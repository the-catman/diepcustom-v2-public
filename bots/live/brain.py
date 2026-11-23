"""Plays the bots of bots-server: connects to its socket and answers each tick's observations with actions.

    python brain.py [--bots bots.json] [--socket ../runs/live.sock] [--device cpu] [--deterministic]
"""
import argparse
import glob
import json
import os
import socket
import struct
import sys
import time

import numpy as np
import torch

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "train"))

import evaluate  # noqa: E402
from common import ENT_DIM, SELF_DIM, TANK_NAMES, build_for, parse_build, tank_id, tanks_of  # noqa: E402
from model import env_action, sample  # noqa: E402
from watch import run_classes  # noqa: E402

VERSION = 1
HELLO, OBS, ACT = 1, 2, 3
REMEMBERS = 1
DETERMINISTIC, FIRST = 1, 2
ROW = struct.Struct("<IBBBB")
ACTION = struct.Struct("<BBBBhHff")
# Listed after the ordinary classes, so a bot's seat starts on a tank a player can fight.
DEV_TANKS = {"Arena Closer", "Mothership", "Destroyer Dominator", "Gunner Dominator", "Trapper Dominator"}


def newest(path):
    """A folder's newest numbered checkpoint, or the file itself."""
    if os.path.isdir(path):
        found = sorted(glob.glob(os.path.join(path, "it[0-9][0-9][0-9][0-9][0-9].pt")))
        if not found:
            raise SystemExit(f"{path} has no numbered checkpoints")
        return found[-1]
    return path


def load_policy(path, device):
    """A checkpoint the server can play, and whether it remembers enemies off screen."""
    evaluate.DEVICE = device
    policy, env = evaluate.load(path)
    policy.requires_grad_(False).eval()
    settings = os.path.join(os.path.dirname(path), "args.json")
    turned = False
    if os.path.exists(settings):
        with open(settings) as f:
            turned = bool(json.load(f).get("symmetry"))
    if not policy.any_class or not turned:
        raise SystemExit(f"{path}: the server only plays bots that play any class and saw the arena turned")
    return policy, env, bool((env.get("remember") or [False])[0])


class Bot:
    """A checkpoint for every class, and specialists that play their own class in its place."""

    def __init__(self, entry, base, device):
        self.name = entry["name"]
        self.device = device
        self.path = newest(os.path.normpath(os.path.join(base, entry["checkpoint"])))
        self.policy, env, remembers = load_policy(self.path, device)
        if entry.get("classes"):
            classes = [tank_id(c) for c in entry["classes"]]
        else:
            classes = run_classes(self.path) or [tanks_of(env)[0]]
        classes.sort(key=lambda t: TANK_NAMES[t] in DEV_TANKS)
        build = parse_build(entry["build"]) if entry.get("build") else None
        self.tanks = [(t, build or build_for(t)) for t in classes]
        self.flags = REMEMBERS if remembers else 0
        self.specialists = {}
        for cls, path in (entry.get("specialists") or {}).items():
            t = tank_id(cls)
            path = newest(os.path.normpath(os.path.join(base, path)))
            policy, _, theirs = load_policy(path, device)
            if t not in classes:
                raise SystemExit(f"{self.name} doesn't play {TANK_NAMES[t]}, so it has no use for {path}")
            if theirs != remembers:
                raise SystemExit(f"{path} and {self.path} must both remember enemies off screen, or neither")
            self.specialists[t] = (path, policy)

    def policy_for(self, tank):
        return self.specialists[tank][1] if tank in self.specialists else self.policy


def hello(bots):
    out = bytearray(struct.pack("<IHHB", VERSION, SELF_DIM, ENT_DIM, len(bots)))

    def tanks(entries):
        out.append(len(entries))
        for t, stats in entries:
            out.append(t)
            out.extend(bytes(stats))

    for b in bots:
        name = b.name.encode()[:255]
        out.append(len(name))
        out.extend(name)
        out.append(b.flags)
        tanks(b.tanks)
    tanks([(t, build_for(t)) for t in sorted(TANK_NAMES) if TANK_NAMES[t]])
    return bytes(out)


def parse_obs(body):
    """An Obs frame -> (seq, rows as (match, slot, bot, tank, flags), the observations as Vec.observe gives them)."""
    seq, n, width = struct.unpack_from("<IHH", body)
    at = 8
    rows = [ROW.unpack_from(body, at + i * ROW.size) for i in range(n)]
    at += n * ROW.size

    def take(dtype, shape):
        nonlocal at
        count = int(np.prod(shape))
        a = np.frombuffer(body, dtype, count, at).reshape(shape)
        at += count * np.dtype(dtype).itemsize
        return a

    obs = {
        "self": take(np.float32, (n, SELF_DIM)),
        "ents": take(np.float32, (n, width, ENT_DIM)),
        "mask": take(np.uint8, (n, width)).astype(bool),
        "targetable": take(np.uint8, (n, width)).astype(bool),
        "tank": np.array([r[3] for r in rows], np.int32),
    }
    return seq, rows, obs


def encode_act(seq, actions):
    """actions: per row (move, fire, repel, target, aim, reach), in the Obs frame's row order."""
    out = bytearray(struct.pack("<IH", seq, len(actions)))
    for move, fire, repel, target, aim, reach in actions:
        out.extend(ACTION.pack(int(move), int(bool(fire)), int(bool(repel)), 0, int(target), 0, float(aim), float(reach)))
    return bytes(out)


def frame(kind, body):
    return struct.pack("<IB", len(body), kind) + body


class Brain:
    def __init__(self, bots, deterministic=False):
        self.bots = bots
        self.deterministic = deterministic
        # Memory per (match, slot), for policies that keep one.
        self.memory = {}

    @torch.no_grad()
    def decide(self, rows, obs):
        actions = [None] * len(rows)
        groups = {}
        for i, (match, slot, bot, tank, flags) in enumerate(rows):
            if flags & FIRST:
                self.memory.pop((match, slot), None)
            det = self.deterministic or bool(flags & DETERMINISTIC)
            special = bot < len(self.bots) and tank in self.bots[bot].specialists
            groups.setdefault((bot, tank if special else -1, det), []).append(i)
        for (bot, tank, det), idx in groups.items():
            if bot >= len(self.bots):
                for i in idx:
                    actions[i] = (0, 0, 0, -1, 0.0, float("nan"))
                continue
            b = self.bots[bot]
            policy = b.policy_for(tank)
            sel = np.array(idx)
            o = {k: torch.from_numpy(np.ascontiguousarray(v[sel])).to(b.device) for k, v in obs.items()}
            state = None
            if policy.memory:
                state = torch.stack([self.memory.get(rows[i][:2], torch.zeros(policy.memory, device=b.device))
                                     for i in idx])
            out, state = policy(o, state)
            a = {k: v.cpu().numpy() for k, v in env_action(sample(out, det)).items()}
            for j, i in enumerate(idx):
                if state is not None:
                    self.memory[rows[i][:2]] = state[j]
                repel = a["repel"][j] if "repel" in a else 0
                reach = a["reach"][j] if "reach" in a else float("nan")
                actions[i] = (a["move"][j], a["fire"][j], repel, a["target"][j], a["aim"][j], reach)
        return actions


def frames(sock):
    buf = bytearray()
    while True:
        while len(buf) >= 5:
            n = struct.unpack_from("<I", buf)[0]
            if len(buf) < 5 + n:
                break
            kind, body = buf[4], bytes(buf[5:5 + n])
            del buf[:5 + n]
            yield kind, body
        chunk = sock.recv(1 << 20)
        if not chunk:
            return
        buf += chunk


def serve(brain, path, greeting):
    while True:
        sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        try:
            sock.connect(path)
        except OSError:
            sock.close()
            time.sleep(1)
            continue
        print(f"connected to {path}", flush=True)
        sock.sendall(frame(HELLO, greeting))
        brain.memory.clear()
        try:
            for kind, body in frames(sock):
                if kind != OBS:
                    continue
                seq, rows, obs = parse_obs(body)
                sock.sendall(frame(ACT, encode_act(seq, brain.decide(rows, obs))))
        except OSError:
            pass
        sock.close()
        print("server went away; waiting for it", flush=True)
        time.sleep(1)


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--bots", default=os.path.join(HERE, "bots.json"))
    p.add_argument("--socket", default=os.path.join(HERE, "..", "runs", "live.sock"))
    p.add_argument("--device", default="cpu", help="cpu by default, since training holds the GPU")
    p.add_argument("--threads", type=int, default=2)
    p.add_argument("--deterministic", action="store_true", help="every bot plays its best move")
    args = p.parse_args()
    torch.set_num_threads(args.threads)
    with open(args.bots) as f:
        entries = json.load(f)
    base = os.path.dirname(os.path.abspath(args.bots))
    bots = []
    for e in entries:
        b = Bot(e, base, args.device)
        bots.append(b)
        print(f"{b.name}: {os.path.relpath(b.path, base)}, "
              f"{', '.join(TANK_NAMES[t] for t, _ in b.tanks)}", flush=True)
        for t, (path, _) in b.specialists.items():
            print(f"  {TANK_NAMES[t]}: {os.path.relpath(path, base)}", flush=True)
    serve(Brain(bots, args.deterministic), args.socket, hello(bots))


if __name__ == "__main__":
    main()
