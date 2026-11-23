"""PPO on duels, with hits paid on the decision that fired.

A hit's reward arrives up to a bullet lifetime after the shot, so the newest `lag` decisions of a
rollout aren't trained on yet: they carry over to the next rollout, and only decisions whose
bullets have all landed or expired get advantages.

With --memory, a GRU carries state across decisions. Training replays chunks of --seq decisions
from the state stored at each chunk's start.

--rival trains against another run going at the same time, e.g. a Factory and an Overlord each
with the other as --rival: a rival:W share of the duels plays snapshots of it, drawn like the
self-play pool from its numbered saves so far plus --rival-anchor, which always stays in. Each run
waits at every --eval-every block until the rival has saved that block, so neither gets ahead.

--frozen adds saved bots, of any class, as opponents that never change: a frozen:W share of the
duels is split between them by weight, and each plays the class it trained as. --holdout bots are
only played at the --frozen-eval-every checks, to see whether what's learned carries over.

--shapes puts the field's shapes in the self-play and live games, as a range of FFA's density; frozen,
scripted and rival opponents and the usual exams play without them. --shape-vs bots are played at the
frozen checks both with shapes and without, and the focus class plays the field with them too.

--sentinels are the classes the focus class plays at each frozen check: on open ground, with shapes, and with
shapes it can't see; the whole field plays at the end without the blind exam.

--ffa adds free-for-all games of --ffa-players tanks alongside the duels: every seat is the policy, except the last one
in --ffa-snapshots of them, which plays a pool snapshot. A tank that dies sits out the rest of its game, and only
decisions made alive are trained on. Advantages are scaled separately for the free-for-all and the duels, whose
returns differ a lot. --remember shows the policy each enemy tank it has seen, while off screen, where it last saw it.

--team adds team games of two teams of --team-size: every seat is the policy, except the second team in
--team-snapshots of them, which a pool snapshot plays. A kill pays the killer and its teammates still alive, the last
team with a tank left wins, and the tanks of it still alive get the win. A tank that dies with a teammate alive hands
its future on: its return carries on as the teammate's. Team games get their own advantage scale too.

--free-lead leaves the lead angle out of the KL that ends an iteration's updates early, and keeps its spread
inside its clamp so it can still learn: a lead spread at its floor makes the smallest change look like a big
one, and ended most iterations after a few updates.

Drone classes (Overseer and the like) also learn to right click and where to put the mouse.
Their drones launch on their own, so drone hits are paid on impact.

Ctrl-C (or SIGTERM) finishes the current iteration, saves and exits; --resume carries on from there.
Resuming restores the weights, optimizer, self-play pool, schedule and random state, but starts fresh
games: the decisions still waiting on their bullets at the stop are dropped.
"""
import argparse
import copy
import json
import os
import signal
import time

import numpy as np
import torch

from common import diepenv, make_config, tank_id, tanks_of, builds_of, build_text, parse_build, versus, DEFAULT_BUILD, \
    KINDS, MOUSE_DRONES, TANK_NAMES, NUM_CLASSES, OLD_SELF_DIM, build_for, parse_classes
from evaluate import evaluate, evaluate_many, describe, evaluate_ffa, describe_ffa, evaluate_team, describe_team, forget, load, \
    shape_results, Actions
from watch import name
from model import Policy, sample, log_prob, lead_log_prob, entropy, env_action, adapt, model_shape, BASE_SELF_DIM, \
    LEAD_LOGSTD

DEVICE = "cuda" if torch.cuda.is_available() else "cpu"
OBS_KEYS = ("self", "ents", "mask", "targetable", "tank")
ENT_KEYS = ("ents", "mask", "targetable")
# Frozen bots driving at most this many rows run on the CPU.
CPU_DRIVER_ROWS = 4
NUMPY_TYPES = {torch.bool: np.bool_, torch.int32: np.int32, torch.float32: np.float32}
# The mouse stats of drone classes, summed over a rollout.
CTRL = ("n", "attract", "repel", "placed", "placed_reach", "free", "free_reach")
ACT_KEYS = ("move", "fire", "target", "aim")
DRONE_KEYS = ("repel", "reach")
ORDER = ["still", "linear", "strafe", "fighter"]


def fetch(act):
    """env_action's tensors as numpy, in one copy from the GPU rather than one per key."""
    host = torch.stack([v.float() for v in act.values()]).cpu().numpy()
    return {k: host[i].astype(NUMPY_TYPES[v.dtype]) for i, (k, v) in enumerate(act.items())}


def parse_mix(text):
    mix = []
    for part in text.split(","):
        kind, _, weight = part.partition(":")
        if kind not in KINDS and kind not in ("frozen", "rival", "live"):
            raise ValueError(f"unknown opponent {kind}")
        mix.append((kind, float(weight or 1)))
    return mix


class Pool:
    """Frozen past versions of the policy for self-play opponents."""

    def __init__(self, model_args, keep):
        self.snapshots = []
        self.keep = keep
        self.policy = Policy(**model_args).to(DEVICE)
        self.policy.requires_grad_(False)

    def add(self, policy):
        self.snapshots.append({k: v.detach().clone() for k, v in policy.state_dict().items()})
        if len(self.snapshots) > self.keep:
            self.snapshots.pop(0)

    def pick(self, rng):
        """Half the time the newest snapshot, otherwise any of them."""
        i = len(self.snapshots) - 1 if rng.random() < 0.5 else int(rng.integers(len(self.snapshots)))
        self.policy.load_state_dict(self.snapshots[i])
        return i


class Rollout:
    def __init__(self, steps, rows, obs, memory, drones=False):
        self.obs = {k: torch.zeros((steps,) + obs[k].shape, dtype=torch.from_numpy(obs[k]).dtype, device=DEVICE)
                    for k in OBS_KEYS}
        self.act = {
            "move": torch.zeros(steps, rows, dtype=torch.long, device=DEVICE),
            "fire": torch.zeros(steps, rows, dtype=torch.bool, device=DEVICE),
            "target": torch.zeros(steps, rows, dtype=torch.long, device=DEVICE),
            "aim": torch.zeros(steps, rows, device=DEVICE),
        }
        if drones:
            self.act["repel"] = torch.zeros(steps, rows, dtype=torch.bool, device=DEVICE)
            self.act["reach"] = torch.zeros(steps, rows, device=DEVICE)
        self.logp = torch.zeros(steps, rows, device=DEVICE)
        # The lead angle's part of it.
        self.lead_logp = torch.zeros(steps, rows, device=DEVICE)
        self.value = torch.zeros(steps, rows, device=DEVICE)
        self.reward = torch.zeros(steps, rows, device=DEVICE)
        self.done = torch.zeros(steps, rows, dtype=torch.bool, device=DEVICE)
        # The tank was alive to make the decision.
        self.valid = torch.ones(steps, rows, dtype=torch.bool, device=DEVICE)
        # The memory going into each decision.
        self.state = torch.zeros(steps, rows, memory, device=DEVICE) if memory else None
        # The row of the teammate a tank that died at that decision hands its future to, or -1.
        self.mate = torch.full((steps, rows), -1, dtype=torch.long, device=DEVICE)

    def put_obs(self, pos, o):
        """Stores one decision's observations. The entity buffers widen when a screen is busier than
        any before, and stay that wide."""
        w = o["ents"].shape[1]
        have = self.obs["ents"].shape[2]
        if w > have:
            wider = -(-w // 16) * 16
            for k in ENT_KEYS:
                # Through the host a step at a time, so the old and the new never fill the GPU together.
                old = self.obs[k].cpu()
                self.obs[k] = None
                torch.cuda.empty_cache()
                grown = torch.zeros(old.shape[:2] + (wider,) + old.shape[3:], dtype=old.dtype, device=DEVICE)
                for t in range(len(old)):
                    grown[t, :, :have] = old[t].to(DEVICE)
                self.obs[k] = grown
        for k in OBS_KEYS:
            if k in ENT_KEYS:
                self.obs[k][pos, :, :w] = o[k]
                self.obs[k][pos, :, w:] = 0
            else:
                self.obs[k][pos] = o[k]

    def tensors(self):
        extra = [self.state] if self.state is not None else []
        return [*self.obs.values(), *self.act.values(), self.logp, self.lead_logp, self.value, self.reward, self.done,
                self.valid, self.mate, *extra]

    def shift(self, start, count):
        """Moves steps [start, start + count) to the front."""
        for t in self.tensors():
            t[:count] = t[start:start + count].clone()


def gae(reward, value, done, last_value, gamma, lam, mate=None):
    """mate[t], where it isn't -1, is the row of the teammate a tank that died at decision t hands its future to:
    from there on its return is the teammate's."""
    steps = reward.shape[0]
    adv = torch.zeros_like(reward)
    running = torch.zeros_like(last_value)
    for t in reversed(range(steps)):
        next_value = last_value if t == steps - 1 else value[t + 1]
        live = (~done[t]).float()
        carried = running
        if mate is not None:
            handed = mate[t] >= 0
            m = mate[t].clamp(min=0)
            next_value = torch.where(handed, next_value[m], next_value)
            carried = torch.where(handed, running[m], running)
            live = torch.where(handed, torch.ones_like(live), live)
        delta = reward[t] + gamma * next_value * live - value[t]
        running = delta + gamma * lam * live * carried
        adv[t] = running
    return adv, adv + value


def load_frozen(text, symmetry):
    """'path[:weight],...' -> one dict per bot: its policy, class, weight and label."""
    bots = []
    for part in filter(None, (text or "").split(",")):
        path, weight = part, 1.0
        head, _, tail = part.rpartition(":")
        if head and tail.replace(".", "", 1).isdigit():
            path, weight = head, float(tail)
        if os.path.basename(path) in ("latest.pt", "resume.pt"):
            raise SystemExit(f"{path}: use a numbered checkpoint, latest.pt keeps changing")
        run_args = os.path.join(os.path.dirname(path), "args.json")
        if symmetry and os.path.exists(run_args):
            with open(run_args) as f:
                if not json.load(f).get("symmetry"):
                    raise SystemExit(f"{path} never saw the arena turned, but this run turns it (--symmetry)")
        policy, env = load(path)
        policy.requires_grad_(False)
        bots.append({"policy": policy, "tank": tanks_of(env)[0], "stats": builds_of(env)[0], "weight": weight,
                     "label": name(path)})
    return bots


class Rival:
    """Snapshots of a run training alongside this one, as the self-play pool keeps of this one."""

    def __init__(self, folder, anchor, every, keep, wait):
        self.folder, self.every, self.keep, self.wait = folder, every, keep, wait
        policy, env = load(anchor)
        self.policy = policy.requires_grad_(False)
        self.tank, self.stats = tanks_of(env)[0], builds_of(env)[0]
        self.model_args = torch.load(anchor, map_location="cpu").get("model_args", {})
        self.anchor = {k: v.clone() for k, v in policy.state_dict().items()}
        self.snapshots = []
        self.newest = 0

    def path(self, it):
        return os.path.join(self.folder, f"it{it:05d}.pt")

    def sync(self, it):
        """Loads the rival's saves up to the block before iteration it, waiting for the last one."""
        block = (it - 1) // self.every * self.every
        if block <= self.newest:
            return
        deadline = time.time() + self.wait
        if not os.path.exists(self.path(block)):
            print(f"waiting for the rival's iteration {block}", flush=True)
        while not os.path.exists(self.path(block)):
            if time.time() > deadline:
                raise SystemExit(f"the rival never saved {self.path(block)}; is it still running?")
            time.sleep(10)
        for b in range(self.newest + self.every, block + 1, self.every):
            if os.path.exists(self.path(b)):
                ckpt = torch.load(self.path(b), map_location=DEVICE)
                if builds_of(ckpt.get("env_args", {}))[0] != self.stats:
                    raise SystemExit(f"{self.path(b)} plays a different build from the rival's anchor")
                if model_shape(ckpt.get("model_args", {})) != model_shape(self.model_args):
                    raise SystemExit(f"{self.path(b)} is a different model from the rival's anchor")
                self.snapshots.append((b, ckpt["model"]))
        self.snapshots = self.snapshots[-self.keep:]
        self.newest = block

    def pick(self, rng):
        """Half the time the newest snapshot, otherwise any of them or the anchor."""
        choices = [(0, self.anchor)] + self.snapshots
        b, state = choices[-1] if rng.random() < 0.5 else choices[int(rng.integers(len(choices)))]
        self.policy.load_state_dict(state)
        return b


CLOSE_SPAWN = dict(spawn_min=600, spawn_max=1200, mirror_spawn=False)


def anchor_args(args, env_args):
    """The settings scripted and frozen opponents play under."""
    return dict(env_args, **CLOSE_SPAWN) if getattr(args, "close_anchors", False) else env_args


def spawn_range(args, it):
    """How far apart self-play and rival duels start at iteration it, ramping over --spawn-ramp iterations."""
    if not args.spawn_ramp:
        return args.spawn_min, args.spawn_max
    lo, hi = (float(x) for x in args.spawn_start.split(":"))
    f = min(1.0, (it - 1) / args.spawn_ramp)
    return lo + f * (args.spawn_min - lo), hi + f * (args.spawn_max - hi)


def shape_args(args, it):
    """Shapes in self-play and live games at iteration it, growing from none over --shape-ramp iterations."""
    if not args.shapes:
        return {}
    lo, hi = (float(x) for x in args.shapes.split(":"))
    f = min(1.0, (it - 1) / args.shape_ramp) if args.shape_ramp else 1.0
    return dict(shapes_min=lo * f, shapes_max=hi * f, shapes_empty=args.shape_empty, pentagon_rich=args.pentagon_rich)


def exam_shapes(args):
    """Every exam game with shapes has --shape-exam times FFA's density, of the field's usual mix."""
    return dict(shapes_min=args.shape_exam, shapes_max=args.shape_exam)


def exam_conditions(args):
    """What a sentinel exam plays under: the extra settings, and the kinds of thing the focus class isn't shown."""
    return {"plain": ({}, ()), "shapes": (exam_shapes(args), ()), "shapes-blind": (exam_shapes(args), ("shapes",))}


def choices(classes, builds):
    """[(class, weight)] -> what Config.choices takes for one slot."""
    return [(t, list(builds.get(t, build_for(t))), w) for t, w in classes]


def ffa_config(args, env_args, snapshot, mine=None, theirs=None):
    """A free-for-all game: the learner's choices in the first seat and the opponents' in the rest, as in a
    duel. With a snapshot, the last seat plays one and isn't learned from."""
    n = args.ffa_players
    c = make_config("self", **dict(env_args, arena_size=args.ffa_arena, max_decisions=args.ffa_decisions,
                                   spawn_min=args.ffa_spawn, mirror_spawn=False))
    c.players = n
    c.kinds = [diepenv.EXTERNAL] * n
    c.tanks = [env_args["tanks"][0]] + [env_args["tanks"][1]] * (n - 1)
    c.stats = [list(env_args["stats"][0])] + [list(env_args["stats"][1])] * (n - 1)
    c.learner = [True] * (n - 1) + [not snapshot]
    c.remember = [args.remember] * n
    if mine:
        c.choices = [mine] + [theirs] * (n - 1)
    return c


def team_config(args, env_args, snapshot, mine=None, theirs=None):
    """A team game, two teams of --team-size: the learner's choices in the first seat and the opponents' in the rest,
    as in a free-for-all. With a snapshot, the second team plays one and isn't learned from."""
    k = args.team_size
    n = 2 * k
    c = make_config("self", **dict(env_args, arena_size=args.team_arena, max_decisions=args.team_decisions,
                                   spawn_min=args.team_spawn_min, spawn_max=args.team_spawn_max, mirror_spawn=False))
    c.players = n
    c.teams = [0] * k + [1] * k
    c.mate_min, c.mate_max = args.mate_min, args.mate_max
    c.loss_reward, c.eliminated_reward = args.team_death, args.team_eliminated
    c.kinds = [diepenv.EXTERNAL] * n
    c.tanks = [env_args["tanks"][0]] + [env_args["tanks"][1]] * (n - 1)
    c.stats = [list(env_args["stats"][0])] + [list(env_args["stats"][1])] * (n - 1)
    c.learner = [True] * k + [not snapshot] * k
    c.remember = [args.remember] * n
    if mine:
        c.choices = [mine] + [theirs] * (n - 1)
    return c


def build_envs(args, env_args, seed, frozen=(), rival=None, mine=None, theirs=None, shapes=None):
    """mine and theirs, when given, are the class choices each game deals the learner's slot and the
    opponent's: the opponent's for scripted, self-play and live games, while frozen bots and the rival
    play their own class. shapes are the shape settings of the self-play and live games. --ffa games come
    after the duels.

    Also returns the games whose opponent is dealt from theirs, the self-play, live and rival games, and
    the self-play and live games, each with its config, for changing what they deal, how far apart they start,
    and their shapes."""
    near = anchor_args(args, env_args)
    mix = parse_mix(args.mix)
    remember, games = getattr(args, "remember", False), getattr(args, "ffa", 0)
    total = sum(w for _, w in mix)
    configs, row_kind = [], []

    def deal(c, opponent=True, policy=False):
        if mine:
            c.choices = [mine, theirs if opponent else []]
        # The policy and its snapshots are shown where enemies were last seen; saved and scripted bots never were.
        c.remember = [remember, remember and policy]
        return c

    for kind, w in mix:
        n = max(1, round(args.duels * w / total))
        if kind == "frozen":
            if not frozen:
                raise SystemExit("the mix has frozen opponents but --frozen names none")
            weights = sum(b["weight"] for b in frozen)
            for b in frozen:
                m = max(1, round(n * b["weight"] / weights))
                configs += [deal(make_config("self", **versus(near, b["tank"], b["stats"])), False)] * m
                row_kind += [b["label"]] * m
            continue
        if kind == "rival":
            if rival is None:
                raise SystemExit("the mix has a rival but --rival names none")
            configs += [deal(make_config("self", **versus(env_args, rival.tank, rival.stats)), False)] * n
            row_kind += ["rival"] * n
            continue
        if kind == "live":
            configs += [deal(make_config("self", **env_args, **(shapes or {})), policy=True)] * n
            row_kind += [kind] * n
            continue
        settings = dict(env_args, **(shapes or {})) if kind == "self" else near
        configs += [deal(make_config(kind, **settings), policy=kind == "self")] * n
        row_kind += [kind] * n
    snapshots = round(games * getattr(args, "ffa_snapshots", 0))
    for i in range(games):
        configs.append(ffa_config(args, env_args, i < snapshots, mine, theirs))
        row_kind.append("ffa-pool" if i < snapshots else "ffa")
    teams = getattr(args, "team", 0)
    team_snapshots = round(teams * getattr(args, "team_snapshots", 0))
    for i in range(teams):
        configs.append(team_config(args, env_args, i < team_snapshots, mine, theirs))
        row_kind.append("team-pool" if i < team_snapshots else "team")
    vec = diepenv.Vec(configs, args.threads)
    vec.seed(seed)
    vec.reset_all()
    # Self-play games have two rows: the learner in slot 0 and a pool opponent in slot 1. In live games both
    # tanks are the learner, and in a free-for-all or a team game every seat but a snapshot's. The learner's seats
    # in a team game against a snapshot are team-pool, and in one where it plays every seat team.
    slot = np.array([s for _, s in vec.row_map])
    duel_kind = np.array(row_kind)[[d for d, _ in vec.row_map]]
    ffa = np.isin(duel_kind, ["ffa", "ffa-pool"])
    team = np.isin(duel_kind, ["team", "team-pool"])
    pooled = ((duel_kind == "ffa-pool") & (slot == getattr(args, "ffa_players", 0) - 1)) \
        | ((duel_kind == "team-pool") & (slot >= getattr(args, "team_size", 0)))
    learning = np.where(ffa | team, ~pooled, (slot == 0) | (duel_kind == "live"))
    duel_kind = np.where(pooled, "self", np.where(ffa, "ffa", duel_kind))
    learner = np.flatnonzero(learning)
    opponent = np.flatnonzero(~learning)
    far = [(d, c) for d, (c, k) in enumerate(zip(configs, row_kind)) if k in ("self", "rival", "live")]
    dealt = [(d, c) for d, (c, k) in enumerate(zip(configs, row_kind)) if mine and (k in KINDS or k == "live")]
    shaped = [(d, c) for d, (c, k) in enumerate(zip(configs, row_kind)) if k in ("self", "live")]
    return vec, mix, duel_kind[learner], learner, opponent, duel_kind[opponent], dealt, far, shaped


def pfsp_weights(results, classes, cap=0.25):
    """Opponent classes by weight times (0.1 + loss + draw / 2) squared, from how the focus class has been doing
    against each, so the ones it struggles with come up most; no class gets over cap of the games. A class
    with no results yet counts as a middling one."""
    raw = {}
    for t, w in classes:
        r = results.get(t)
        hard = 0.5 if r is None else r["loss"] + 0.5 * r["draw"]
        raw[t] = w * (0.1 + hard) ** 2
    cap = max(cap, 1 / len(raw))
    capped = set()
    while True:
        # What the capped classes don't take is shared by the rest in proportion to their weights.
        free = 1 - cap * len(capped)
        rest = sum(v for t, v in raw.items() if t not in capped) or 1.0
        share = {t: cap if t in capped else free * v / rest for t, v in raw.items()}
        over = {t for t, v in share.items() if t not in capped and v > cap}
        if not over:
            return share
        capped |= over


def save_atomic(obj, path):
    """A stop mid-write leaves the previous save intact."""
    torch.save(obj, path + ".tmp")
    os.replace(path + ".tmp", path)


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--out", required=True)
    p.add_argument("--resume", action="store_true",
                   help="continue the run in --out with its saved settings; only --iters, --threads and --lr can change")
    p.add_argument("--save-every", type=int, default=25, help="iterations between resume saves")
    p.add_argument("--init", default=None)
    p.add_argument("--mix", default="fighter", help="opponent:weight,... e.g. still:1,linear:1")
    p.add_argument("--duels", type=int, default=256)
    p.add_argument("--threads", type=int, default=None, help="default 16")
    p.add_argument("--rollout", type=int, default=128)
    p.add_argument("--lag", type=int, default=24)
    p.add_argument("--iters", type=int, default=None,
                   help="default 500; raising it on --resume stretches the learning rate schedule")
    p.add_argument("--lr", type=float, default=None, help="default 3e-4")
    p.add_argument("--warmup", type=int, default=10, help="iterations to ramp the learning rate up over")
    p.add_argument("--lr-iters", type=int, default=None,
                   help="iterations the learning rate fades to a tenth over, default --iters")
    p.add_argument("--free-lead", action="store_true",
                   help="leave the lead angle out of the KL that ends updates early, and keep its spread learning")
    p.add_argument("--gamma", type=float, default=0.99)
    p.add_argument("--lam", type=float, default=0.95)
    p.add_argument("--clip", type=float, default=0.2)
    p.add_argument("--epochs", type=int, default=4)
    p.add_argument("--minibatch", type=int, default=4096, help="decisions per minibatch")
    p.add_argument("--micro", type=int, default=1024, help="decisions per forward pass within a minibatch, 0 for all")
    p.add_argument("--entropy", type=float, default=0.01)
    p.add_argument("--vf", type=float, default=0.5)
    p.add_argument("--grad-clip", type=float, default=0.5)
    p.add_argument("--target-kl", type=float, default=0.03, help="stop an iteration's updates past this, 0 for never")
    p.add_argument("--memory", type=int, default=0, help="GRU size, 0 for none")
    p.add_argument("--last-seen", action="store_true",
                   help="also show the tank where it is on the map and where it last saw the enemy")
    p.add_argument("--seq", type=int, default=16, help="decisions per training chunk with memory")
    p.add_argument("--width", type=int, default=128)
    p.add_argument("--layers", type=int, default=2)
    p.add_argument("--eval-every", type=int, default=25)
    p.add_argument("--eval-episodes", type=int, default=400)
    p.add_argument("--eval-opponent", default=None, help="defaults to the hardest opponent in the mix")
    p.add_argument("--eval-vs", default=None, help="a checkpoint to also play head-to-head at each eval")
    p.add_argument("--frozen", default=None, help="saved bots to train against: path[:weight],...")
    p.add_argument("--holdout", default=None, help="saved bots only played at the frozen checks: path,...")
    p.add_argument("--rival", default=None, help="folder of a run training alongside this one")
    p.add_argument("--rival-anchor", default=None, help="a checkpoint of the rival kept in its pool for good")
    p.add_argument("--rival-wait", type=float, default=4 * 3600, help="seconds to wait for the rival before giving up")
    p.add_argument("--frozen-eval-every", type=int, default=50)
    p.add_argument("--frozen-eval-episodes", type=int, default=200)
    p.add_argument("--pool-every", type=int, default=20, help="iterations between self-play snapshots")
    p.add_argument("--pool-keep", type=int, default=20)
    p.add_argument("--seed", type=int, default=1)
    # Environment
    p.add_argument("--arena", type=float, default=3000)
    p.add_argument("--max-decisions", type=int, default=375)
    p.add_argument("--spawn-min", type=float, default=600, help="closest the two tanks start")
    p.add_argument("--spawn-max", type=float, default=1200)
    p.add_argument("--mirror-spawn", action="store_true", help="start the tanks mirrored through the centre")
    p.add_argument("--close-anchors", action="store_true",
                   help="scripted and frozen opponents still start 600 to 1200 apart, anywhere on the map")
    p.add_argument("--spawn-ramp", type=int, default=0,
                   help="iterations over which self-play and rival spawns widen from --spawn-start to the full range")
    p.add_argument("--spawn-start", default="1500:3000", help="min:max spawn distance at the ramp's start")
    p.add_argument("--aim-noise", type=float, default=0.0)
    p.add_argument("--no-credit", action="store_true", help="pay hits on impact instead of on the shot")
    p.add_argument("--dealt-scale", type=float, default=0.5)
    p.add_argument("--taken-scale", type=float, default=0.5)
    p.add_argument("--step-cost", type=float, default=0.0,
                   help="taken off every decision, so waiting out the clock costs something; 0.001 makes a full draw -0.375")
    p.add_argument("--symmetry", action="store_true", help="train on the arena turned and mirrored at random")
    p.add_argument("--entity-cap", type=int, default=diepenv.ENT_MAX,
                   help="most on-screen objects each tank is shown; the first bots trained with 40")
    p.add_argument("--tank", default="basic", help="the class to play, e.g. sniper or machine-gun")
    p.add_argument("--opp-tank", default=None, help="the opponents' class, defaults to --tank")
    p.add_argument("--build", default=DEFAULT_BUILD,
                   help="stat points in diep's order, regen first and movement last; scripted and self-play "
                        "opponents get it too, frozen bots and the rival keep their own")
    # One policy for many classes
    p.add_argument("--classes", default=None,
                   help="train one policy for every class: class:weight,... dealt to its tank each game, e.g. "
                        "fighter:3,penta-shot,spread-shot; each plays its usual build (common.BUILDS)")
    p.add_argument("--opp-classes", default=None,
                   help="class:weight,... dealt to scripted, self-play and live opponents; defaults to --classes "
                        "evenly")
    p.add_argument("--focus", default=None, help="the class the run is for, defaults to the first of --classes; "
                                                 "evals play as it")
    p.add_argument("--pfsp", action="store_true",
                   help="deal the opponents the focus class does worst against more often")
    p.add_argument("--mirror", default=None,
                   help="saved bots played at the frozen checks with the policy as their own class: path,...")
    # Shapes
    p.add_argument("--shapes", default=None,
                   help="shapes in self-play and live games as min:max times FFA's density, each game drawing one")
    p.add_argument("--shape-empty", type=float, default=0.0, help="share of those games with no shapes at all")
    p.add_argument("--pentagon-rich", type=float, default=0.0,
                   help="share of the games with shapes that are 20 to 40%% pentagons instead of 4%%")
    p.add_argument("--shape-ramp", type=int, default=0, help="iterations over which shapes grow from none to --shapes")
    p.add_argument("--shape-exam", type=float, default=1.0, help="times FFA's density in the exams with shapes")
    p.add_argument("--shape-vs", default=None,
                   help="saved bots played at the frozen checks both with shapes and without: path,...")
    # Free-for-all
    p.add_argument("--ffa", type=int, default=0, help="free-for-all games, alongside the --duels")
    p.add_argument("--ffa-players", type=int, default=4)
    p.add_argument("--ffa-arena", type=float, default=6000)
    p.add_argument("--ffa-decisions", type=int, default=900)
    p.add_argument("--ffa-spawn", type=float, default=1500, help="closest two free-for-all tanks start")
    p.add_argument("--ffa-snapshots", type=float, default=0.5,
                   help="share of the free-for-all games whose last seat plays a pool snapshot")
    p.add_argument("--ffa-step-cost", type=float, default=0.001,
                   help="--step-cost in the free-for-all, small enough that dying never beats surviving")
    p.add_argument("--ffa-vs", default=None,
                   help="a saved bot in every other seat of a free-for-all the policy plays at the frozen checks")
    p.add_argument("--ffa-exam-episodes", type=int, default=200)
    # Teams
    p.add_argument("--team", type=int, default=0, help="team games, alongside the --duels and --ffa games")
    p.add_argument("--team-size", type=int, default=2)
    p.add_argument("--team-arena", type=float, default=6000)
    p.add_argument("--team-decisions", type=int, default=900)
    p.add_argument("--team-spawn-min", type=float, default=2000, help="closest the two teams' centres start")
    p.add_argument("--team-spawn-max", type=float, default=4500)
    p.add_argument("--mate-min", type=float, default=300, help="closest teammates start")
    p.add_argument("--mate-max", type=float, default=600)
    p.add_argument("--team-snapshots", type=float, default=0.5,
                   help="share of the team games whose second team plays a pool snapshot")
    p.add_argument("--team-step-cost", type=float, default=0.001, help="--step-cost in the team games")
    p.add_argument("--team-death", type=float, default=0.5, help="what dying costs a tank in a team game")
    p.add_argument("--team-eliminated", type=float, default=0.5, help="what the last of a team to die loses on top")
    p.add_argument("--team-vs", default=None,
                   help="a saved bot playing the other team in team games the policy plays at its exams")
    p.add_argument("--team-exam-every", type=int, default=50)
    p.add_argument("--team-exam-episodes", type=int, default=200)
    p.add_argument("--kill-reward", type=float, default=0.0,
                   help="paid to a tank's killer in every game, on top of --win for the last one left")
    p.add_argument("--remember", action="store_true",
                   help="show the policy each enemy tank it has seen, while off screen, where it last saw it")
    p.add_argument("--sentinels", default=None,
                   help="classes the focus class plays at each frozen check on open ground and with shapes, seeing "
                        "them or not; the whole field plays only at the end")
    args = p.parse_args()

    out = os.path.abspath(args.out)
    resume_path = os.path.join(out, "resume.pt")
    saved = None
    if args.resume:
        if not os.path.exists(resume_path):
            raise SystemExit(f"{out} has no resume.pt to continue from")
        with open(os.path.join(out, "args.json")) as f:
            for k, v in json.load(f).items():
                if k in ("out", "resume") or (k in ("iters", "threads", "lr") and getattr(args, k) is not None):
                    continue
                setattr(args, k, v)
        saved = torch.load(resume_path, map_location=DEVICE, weights_only=False)
    elif os.path.exists(os.path.join(out, "log.jsonl")):
        raise SystemExit(f"{out} already has a run: pass --resume to continue it, or pick another --out")
    args.iters = args.iters or 500
    args.threads = args.threads or 16
    args.lr = 3e-4 if args.lr is None else args.lr
    start = saved["iter"] if saved else 0
    os.makedirs(out, exist_ok=True)
    with open(os.path.join(out, "args.json"), "w") as f:
        json.dump({k: v for k, v in vars(args).items() if k != "resume"}, f, indent=2)
    torch.manual_seed(args.seed)
    torch.set_num_threads(4)

    shared = bool(args.classes)
    if args.pfsp and not shared:
        raise SystemExit("--pfsp deals opponent classes, so it needs --classes")
    if args.sentinels and not shared:
        raise SystemExit("--sentinels are classes for the shared policy to play, so they need --classes")
    mine = theirs = None
    if shared:
        classes = parse_classes(args.classes)
        opp_classes = parse_classes(args.opp_classes) if args.opp_classes else [(t, 1.0) for t, _ in classes]
        focus = tank_id(args.focus) if args.focus else classes[0][0]
        builds = {t: build_for(t) for t, _ in classes + opp_classes + [(focus, 1)]}
        mine, theirs = choices(classes, builds), choices(opp_classes, builds)
        print("classes " + ", ".join(f"{TANK_NAMES[t]} x{w:g} {build_text(builds[t])}" for t, w in classes)
              + " / opponents " + ", ".join(f"{TANK_NAMES[t]} x{w:g}" for t, w in opp_classes), flush=True)
        args.tank, args.build = str(focus), build_text(builds[focus])
    env_args = dict(arena_size=args.arena, max_decisions=args.max_decisions, aim_noise=args.aim_noise,
                    credit_shots=not args.no_credit, dealt_scale=args.dealt_scale, taken_scale=args.taken_scale,
                    tanks=[tank_id(args.tank), tank_id(args.opp_tank or args.tank)], entity_cap=args.entity_cap,
                    stats=[parse_build(args.build)] * 2, spawn_min=args.spawn_min, spawn_max=args.spawn_max,
                    mirror_spawn=args.mirror_spawn)
    if args.kill_reward:
        env_args["kill_reward"] = args.kill_reward
    # Exams show the policy where enemies were last seen, and the saved bots it plays nothing they never saw.
    env_args["remember"] = [args.remember, False]
    frozen = load_frozen(args.frozen, args.symmetry)
    rival = None
    if args.rival:
        if not args.rival_anchor:
            raise SystemExit("--rival needs --rival-anchor, the rival's starting checkpoint")
        rival = Rival(args.rival, args.rival_anchor, args.eval_every, args.pool_keep, args.rival_wait)
    holdout = load_frozen(args.holdout, args.symmetry)
    mirror = load_frozen(args.mirror, args.symmetry)
    shape_vs = load_frozen(args.shape_vs, args.symmetry)
    print(f"build {args.build}" + "".join(f", {b['label']} {build_text(b['stats'])}" for b in frozen + holdout)
          + (f", rival {build_text(rival.stats)}" if rival else ""), flush=True)
    # A resumed run gets new games rather than replaying the first ones.
    vec, mix, row_kind, learner, opponent, opp_kind, dealt, far, shaped = build_envs(
        args, dict(env_args, symmetry=args.symmetry), args.seed + 7919 * start, frozen, rival, mine, theirs,
        shape_args(args, start + 1))
    rows = len(learner)
    learner_index = np.full(vec.rows, -1)
    learner_index[learner] = np.arange(rows)
    # Results by kind of opponent count the tank in slot 0; a live game's other tank is the same learner.
    first_slot = np.array([s for _, s in vec.row_map])[learner] == 0
    # Free-for-all and team seats pay their own step cost and get their own advantage scale.
    ffa_row = row_kind == "ffa"
    team_row = np.isin(row_kind, ["team", "team-pool"])
    team_pool = row_kind == "team-pool"
    step_cost = np.where(ffa_row, args.ffa_step_cost,
                         np.where(team_row, getattr(args, "team_step_cost", 0.0), args.step_cost)).astype(np.float32)
    game_of = np.array([d for d, _ in vec.row_map])
    slot_of = np.array([s for _, s in vec.row_map])
    # The row of each game's seat, for finding a teammate's.
    row_at = np.full((vec.duels, diepenv.MAX_PLAYERS), -1)
    row_at[game_of, slot_of] = np.arange(vec.rows)
    # Each learning team's first seat, which the team's results are counted from.
    team_head = team_row & (slot_of[learner] % max(getattr(args, "team_size", 0), 1) == 0)
    ffa_games = np.unique(game_of[learner[ffa_row]])
    # Decisions each free-for-all game has run, and the decision of its first kill (-1 for none yet).
    game_age = np.zeros(vec.duels, int)
    first_kill = np.full(vec.duels, -1)
    kinds = [k for k, _ in mix if k != "frozen"] + [b["label"] for b in frozen if "frozen" in dict(mix)]
    # Games against the policy itself, live or a pool snapshot, which the results by class come from.
    by_class = np.isin(row_kind, ["live", "self"])
    # How the focus class has been doing against each opponent class, for --pfsp.
    pfsp = {t: None for t, _ in opp_classes} if shared else {}
    scripted = [k for k, _ in mix if k in ORDER]
    eval_opponent = args.eval_opponent or (max(scripted, key=ORDER.index) if scripted else "fighter")
    rng = np.random.default_rng(args.seed)
    reference, ref_env = load(args.eval_vs) if args.eval_vs else (None, {})
    # The reference plays the class it trained as.
    vs_args = versus(anchor_args(args, env_args), tanks_of(ref_env)[0], builds_of(ref_env)[0])

    drones = shared or env_args["tanks"][0] in MOUSE_DRONES
    model_args = dict(d=args.width, heads=4, layers=args.layers, memory=args.memory, drones=drones,
                      self_dim=OLD_SELF_DIM if args.last_seen else BASE_SELF_DIM)
    if shared:
        model_args.update(self_dim=diepenv.SELF_DIM, ent_dim=diepenv.ENT_DIM, classes=NUM_CLASSES, any_class=True)
    if saved:
        # A run from before an input was added keeps the inputs it had.
        model_args.update(self_dim=saved["model"]["self_enc.0.weight"].shape[1],
                          ent_dim=saved["model"]["ent_enc.0.weight"].shape[1])
    act_keys = ACT_KEYS + (DRONE_KEYS if drones else ())
    policy = Policy(**model_args).to(DEVICE)
    if args.init and not saved:
        policy.load_state_dict(adapt(torch.load(args.init, map_location=DEVICE)["model"], policy))
    if args.free_lead:
        with torch.no_grad():
            policy.lead_logstd.clamp_(*LEAD_LOGSTD)
    opt = torch.optim.Adam(policy.parameters(), lr=args.lr, eps=1e-5)
    pool = None
    pool_rows = opponent[opp_kind == "self"]
    if len(pool_rows):
        pool = Pool(model_args, args.pool_keep)
        pool.add(policy)
    total_steps = 0
    if saved:
        policy.load_state_dict(saved["model"])
        opt.load_state_dict(saved["opt"])
        if pool:
            pool.snapshots = saved["pool"]
        rng.bit_generator.state = saved["rng"]
        torch.set_rng_state(saved["torch_rng"].cpu())
        if saved["cuda_rng"] is not None and DEVICE == "cuda":
            torch.cuda.set_rng_state_all([r.cpu() for r in saved["cuda_rng"]])
        pfsp.update(saved.get("pfsp") or {})
        total_steps = saved["total_steps"]
        print(f"resuming after iteration {start}", flush=True)

    def resume_state(it):
        return {"iter": it, "total_steps": total_steps, "model": policy.state_dict(), "opt": opt.state_dict(),
                "pool": pool.snapshots if pool else None, "rng": rng.bit_generator.state,
                "torch_rng": torch.get_rng_state(), "pfsp": pfsp,
                "cuda_rng": torch.cuda.get_rng_state_all() if DEVICE == "cuda" else None}

    stopping = []

    def request_stop(signum, frame):
        stopping.append(signum)
        print("stopping after this iteration", flush=True)
        # A second one quits at once.
        signal.signal(signum, signal.SIG_DFL)

    signal.signal(signal.SIGINT, request_stop)
    signal.signal(signal.SIGTERM, request_stop)

    from torch.utils.tensorboard import SummaryWriter
    # Drops events logged after the save being resumed from.
    writer = SummaryWriter(out, purge_step=start + 1 if start else None)
    log = open(os.path.join(out, "log.jsonl"), "a")

    T, L = args.rollout, args.lag
    K = args.seq if args.memory else 1
    assert T % K == 0, "--rollout must be a multiple of --seq"
    full = vec.observe()
    obs = {k: v[learner] for k, v in full.items()}
    buf = Rollout(T + L, rows, obs, args.memory, drones)
    state = policy.initial_state(rows, DEVICE)
    learner_t = torch.from_numpy(learner).to(DEVICE)
    team_t = learner_t[torch.from_numpy(team_row).to(DEVICE)]

    def driver(policy, r):
        return [policy, r, policy.initial_state(len(r), DEVICE), torch.from_numpy(r).to(DEVICE)]

    # Who drives the opponent rows: [policy, rows, memory state, rows on the GPU] for the pool and each frozen
    # bot. A frozen bot with only a few rows drives them from a copy on the CPU, with None for the GPU rows.
    drivers = [driver(pool.policy, pool_rows)] if pool else []
    for b in frozen:
        r = opponent[opp_kind == b["label"]]
        if len(r) > CPU_DRIVER_ROWS:
            drivers.append(driver(b["policy"], r))
        elif len(r):
            on_cpu = copy.deepcopy(b["policy"]).cpu()
            drivers.append([on_cpu, r, on_cpu.initial_state(len(r), "cpu"), None])
    rival_rows = opponent[opp_kind == "rival"]
    if len(rival_rows):
        drivers.append(driver(rival.policy, rival_rows))
    actions = Actions(vec.rows)
    pos = 0
    sentinels = parse_classes(args.sentinels) if args.sentinels else []
    ffa_vs = load(args.ffa_vs)[0].requires_grad_(False) if args.ffa_vs else None
    team_vs = load(args.team_vs)[0].requires_grad_(False) if getattr(args, "team_vs", None) else None

    def ffa_exam_config():
        """The focus class in the first seat, and the opponents' classes in the rest, which never remember."""
        c = ffa_config(args, env_args, False, [(focus, list(builds[focus]), 1.0)] if shared else None, theirs)
        c.remember = [args.remember] + [False] * (args.ffa_players - 1)
        return c

    def team_exam_config():
        """Both teams dealt the opponents' classes, so the saved bot's team starts out as strong."""
        return team_config(args, env_args, False, theirs, theirs)

    for it in range(start + 1, args.iters + 1):
        frac = min(1.0, (it - 1) / (getattr(args, "lr_iters", None) or args.iters))
        for g in opt.param_groups:
            g["lr"] = args.lr * (1 - 0.9 * frac) * min(1.0, it / max(args.warmup, 1))
        if args.spawn_ramp:
            lo, hi = spawn_range(args, it)
            for d, c in far:
                c.spawn_min, c.spawn_max = lo, hi
                vec.set_duel_config(d, c)
        if args.shapes and args.shape_ramp and it <= args.shape_ramp + 1:
            for d, c in shaped:
                for k, v in shape_args(args, it).items():
                    setattr(c, k, v)
                vec.set_duel_config(d, c)
        if args.pfsp and it > 1:
            weights = pfsp_weights(pfsp, opp_classes)
            dealt_now = choices(list(weights.items()), builds)
            for d, c in dealt:
                c.choices = [mine, dealt_now]
                vec.set_duel_config(d, c)
        stats = {k: {"ep": 0, "won": 0, "lost": 0, "timeout": 0, "dealt": 0.0, "taken": 0.0, "shots": 0, "hits": 0,
                     "met": 0, "first_seen": 0, "shape_taken": 0.0, "shape_blocked": 0, "died_to_shape": 0}
                 for k in kinds}
        # Results by (class played, class faced) in games against the policy itself.
        matchups = {}
        timers = {"policy": 0.0, "opponents": 0.0, "env": 0.0}
        dropped = 0
        # How a drone class uses the mouse: clicks, and where it puts it while clicking.
        ctrl = torch.zeros(len(CTRL), dtype=torch.float64, device=DEVICE)
        # Objects on the learner's screen, and how often the entity cap cut some off.
        screen = {"n": 0, "sum": 0, "max": 0, "cut": 0}
        # The learner's free-for-all seats, and the games they're in.
        ffa = {"ep": 0, "won": 0, "lost": 0, "timeout": 0, "apart": 0, "kills": 0, "stolen": 0, "decisions": 0,
               "alive": 0, "games": 0, "timed_out": 0, "no_kill": 0, "first_kill": []}
        # The learner's team seats. Results per team, against a snapshot and against itself; wins with the whole team
        # alive; alive decisions with a teammate alive, and of those with it on screen; kills, a teammate's kills of
        # a tank the seat had worn down, and aiming at a tank, and at one a teammate aimed at too.
        team = {"games": 0, "timeouts": 0, "pool_games": 0, "pool_won": 0, "pool_lost": 0, "wins": 0, "whole": 0,
                "with_mate": 0, "together": 0, "kills": 0, "assists": 0, "targeted": 0, "same": 0}
        if pool:
            if it > 1 and (it - 1) % args.pool_every == 0:
                pool.add(policy)
            picked = pool.pick(rng)
        if rival:
            rival.sync(it)
            rival_picked = rival.pick(rng)
        together = torch.zeros((), dtype=torch.long, device=DEVICE)
        t0 = time.time()
        while pos < T + L:
            tp = time.time()
            # Every row's observations go up once, and each driver takes its rows on the GPU.
            g = {k: torch.from_numpy(full[k]).to(DEVICE) for k in OBS_KEYS}
            o = {k: v[learner_t] for k, v in g.items()}
            if state is not None:
                buf.state[pos] = state
            with torch.no_grad():
                pout, state = policy(o, state)
                a = sample(pout)
                lp = log_prob(pout, a)
                buf.lead_logp[pos] = lead_log_prob(pout, a)
            buf.put_obs(pos, o)
            alive = obs["alive"]
            buf.valid[pos] = torch.from_numpy(alive).to(DEVICE)
            if team_row.any():
                e = g["ents"][team_t]
                shown = (g["mask"][team_t] & (e[..., diepenv.ALLY] == 1) & (e[..., 18] == 1)
                         & (e[..., diepenv.REMEMBERED] == 0)).any(1)
                paired = alive[team_row] & (obs["self"][team_row][:, diepenv.MATES_LEFT] > 0)
                team["with_mate"] += int(paired.sum())
                together += (torch.from_numpy(paired).to(DEVICE) & shown).sum()
            for k in act_keys:
                buf.act[k][pos] = a[k]
            buf.logp[pos] = lp
            buf.value[pos] = pout["value"]
            if drones:
                # Only tanks with mouse drones count.
                mouse = a.get("mouse", torch.ones_like(a["fire"])) & buf.valid[pos]
                clicked = (a["fire"] | a["repel"]) & mouse
                placed = clicked & (a["target"] > 0)
                free = clicked & (a["target"] == 0)
                ctrl += torch.stack([mouse.sum(), (a["fire"] & mouse).sum(), (a["repel"] & mouse).sum(), placed.sum(),
                                     torch.where(placed, a["reach"], 0).sum(), free.sum(),
                                     torch.where(free, a["reach"], 0).sum()]).double()
            actions.set(learner, fetch(env_action(a)))
            to = time.time()
            timers["policy"] += to - tp
            for d in drivers:
                if d[3] is None:
                    oo = {k: torch.from_numpy(full[k][d[1]]) for k in OBS_KEYS}
                else:
                    oo = {k: v[d[3]] for k, v in g.items()}
                with torch.no_grad():
                    oout, d[2] = d[0](oo, d[2])
                    actions.set(d[1], fetch(env_action(sample(oout))))
            te = time.time()
            timers["opponents"] += te - to
            # The classes of the games being played, before the step deals the ended ones new ones.
            playing, facing = obs["tank"], obs["enemy_tank"]
            full = actions.step(vec)
            timers["env"] += time.time() - te
            for d in drivers:
                forget(d[2], full["done"][d[1]])
            obs = {k: v[learner] for k, v in full.items() if not k.startswith("credit_") and k not in ENT_KEYS}
            forget(state, obs["done"])
            ended_live = obs["done"] & by_class
            seen = obs["seen"][obs["alive"]]
            screen["n"] += len(seen)
            screen["sum"] += int(seen.sum())
            screen["max"] = max(screen["max"], int(seen.max(initial=0)))
            screen["cut"] += int((seen > args.entity_cap).sum())
            buf.reward[pos] = torch.from_numpy(obs["reward"] - step_cost).to(DEVICE)
            buf.done[pos] = torch.from_numpy(obs["done"]).to(DEVICE)
            if team_row.any():
                handed = team_row & (obs["handoff"] >= 0)
                mate = np.full(rows, -1)
                mate[handed] = learner_index[row_at[game_of[learner[handed]], obs["handoff"][handed]]]
                buf.mate[pos] = torch.from_numpy(mate).to(DEVICE)
                over = team_head & obs["game_over"]
                team["games"] += int(over.sum())
                team["timeouts"] += int((over & ~obs["team_won"] & ~obs["team_lost"]).sum())
                team["pool_games"] += int((over & team_pool).sum())
                team["pool_won"] += int((over & team_pool & obs["team_won"]).sum())
                team["pool_lost"] += int((over & team_pool & obs["team_lost"]).sum())
                team["wins"] += int((over & obs["team_won"]).sum())
                team["whole"] += int((over & obs["team_won"] & (obs["team_left"] == args.team_size)).sum())
                for k, key in (("kills", "kills"), ("assists", "assists"), ("targeted", "targeted"), ("same", "same_target")):
                    team[k] += int(obs[key][team_row].sum())
            credited = learner_index[full["credit_row"]] if len(full["credit_row"]) else np.zeros(0, int)
            if len(credited):
                at = pos - full["credit_ago"]
                ok = (at >= 0) & (credited >= 0)
                dropped += int(((at < 0) & (credited >= 0)).sum())
                buf.reward.index_put_(
                    (torch.from_numpy(at[ok]).long().to(DEVICE), torch.from_numpy(credited[ok]).long().to(DEVICE)),
                    torch.from_numpy(full["credit_value"][ok]).to(DEVICE), accumulate=True)
            for mine_t, their_t, ended_row in zip(playing[ended_live], facing[ended_live], np.flatnonzero(ended_live)):
                m = matchups.setdefault((int(mine_t), int(their_t)), {"ep": 0, "won": 0, "lost": 0})
                m["ep"] += 1
                m["won"] += int(obs["won"][ended_row])
                m["lost"] += int(obs["lost"][ended_row])
            for kind in kinds:
                m = (row_kind == kind) & first_slot
                s = stats[kind]
                ended = obs["done"] & m
                s["ep"] += int(ended.sum())
                s["won"] += int((obs["won"] & ended).sum())
                s["lost"] += int((obs["lost"] & ended).sum())
                s["timeout"] += int((obs["timed_out"] & ended).sum())
                met = ended & (obs["first_seen"] >= 0)
                s["met"] += int(met.sum())
                s["first_seen"] += int(obs["first_seen"][met].sum())
                s["dealt"] += float(obs["dealt"][m].sum())
                s["taken"] += float(obs["taken"][m].sum())
                s["shots"] += int(obs["shots"][m].sum())
                s["hits"] += int(obs["hits"][m].sum())
                s["shape_taken"] += float(obs["shape_taken"][m].sum())
                s["shape_blocked"] += int(obs["shape_blocked"][m].sum())
                s["died_to_shape"] += int((obs["died_to_shape"] & ended).sum())
            if len(ffa_games):
                ended = obs["done"] & ffa_row
                for k, key in (("ep", None), ("won", "won"), ("lost", "lost"), ("timeout", "timed_out"),
                               ("apart", "apart")):
                    ffa[k] += int(ended.sum() if key is None else (obs[key] & ended).sum())
                ffa["kills"] += int(obs["kills"][ffa_row].sum())
                ffa["stolen"] += int(obs["stolen"][ffa_row].sum())
                ffa["decisions"] += int(ffa_row.sum())
                ffa["alive"] += int((alive & ffa_row).sum())
                game_age[ffa_games] += 1
                killed = np.bincount(game_of, full["kills"], vec.duels)[ffa_games] > 0
                first = ffa_games[killed & (first_kill[ffa_games] < 0)]
                first_kill[first] = game_age[first]
                over = ffa_games[np.bincount(game_of, full["game_over"], vec.duels)[ffa_games] > 0]
                ffa["games"] += len(over)
                ffa["timed_out"] += int((np.bincount(game_of, full["timed_out"], vec.duels)[over] > 0).sum())
                ffa["no_kill"] += int((first_kill[over] < 0).sum())
                ffa["first_kill"] += [int(x) for x in first_kill[over] if x >= 0]
                game_age[over] = 0
                first_kill[over] = -1
            pos += 1
        collect_time = time.time() - t0
        team["together"] = int(together)
        total_steps += T * rows

        # Train on the first T decisions; their bullets have had `lag` decisions to land.
        adv, ret = gae(buf.reward[:T], buf.value[:T], buf.done[:T], buf.value[T], args.gamma, args.lam,
                       buf.mate[:T] if team_row.any() else None)
        valid = buf.valid[:T]
        # A free-for-all pays several times what a duel does, so each kind of game is scaled on its own.
        for group in (ffa_row, team_row, ~(ffa_row | team_row)):
            m = valid & torch.from_numpy(group).to(DEVICE)[None, :]
            if m.any():
                adv[m] = (adv[m] - adv[m].mean()) / (adv[m].std() + 1e-8)
        mean_reward = float(buf.reward[:T][valid].mean())
        # Chunks of K decisions of one row; without memory each decision is its own chunk. Only those
        # starting with the tank alive are trained on, and within one only the decisions it was alive for.
        chunks = (T // K) * rows
        start_t = torch.arange(0, T, K, device=DEVICE).repeat_interleave(rows)
        chunk_row = torch.arange(rows, device=DEVICE).repeat(T // K)
        usable = torch.nonzero(valid[start_t, chunk_row]).squeeze(1)
        offsets = torch.arange(K, device=DEVICE)[:, None]
        per_batch = max(1, args.minibatch // K)
        per_micro = max(1, (args.micro or args.minibatch) // K)
        # A micro piece costs about rows x tokens squared in attention; keep each to what --micro rows
        # cost with the first bots' 40 entities, so busy screens run in fewer rows at a time.
        budget = per_micro * K * 41 ** 2
        tokens = buf.obs["mask"][:T].sum(-1)
        sums = {"pg": 0.0, "vf": 0.0, "ent": 0.0, "kl": 0.0, "kl_lead": 0.0, "clip": 0.0}
        updates = 0
        stopped = False
        t1 = time.time()
        for _ in range(args.epochs):
            if stopped:
                break
            perm = usable[torch.randperm(len(usable), device=DEVICE)]
            for b0 in range(0, len(perm), per_batch):
                sel = perm[b0:b0 + per_batch]
                opt.zero_grad(set_to_none=True)
                kl = kl_lead = kl_rest = 0.0
                # Pieces of the minibatch, gradients summed: the same update with less memory. Sorted by
                # how busy each screen was, so quiet decisions aren't padded out to the busiest.
                busy = tokens[start_t[sel][None, :] + offsets, chunk_row[sel][None, :].expand(K, -1)].amax(0)
                order = torch.argsort(busy)
                sel, busy = sel[order], busy[order].tolist()
                pieces, i = [], 0
                while i < len(sel):
                    j = i + 1
                    while j < len(sel) and (j + 1 - i) * K * (busy[j] + 1) ** 2 <= budget:
                        j += 1
                    pieces.append((sel[i:j], max(int(busy[j - 1]), 1)))
                    i = j
                for part, width in pieces:
                    w = len(part) / len(sel)
                    ti = start_t[part][None, :] + offsets
                    ri = chunk_row[part][None, :].expand(K, -1)
                    # Only the entity slots the part uses, which is all trim() would keep anyway.
                    mo = {k: (buf.obs[k][:, :, :width] if k in ENT_KEYS else buf.obs[k])[ti, ri].flatten(0, 1)
                          for k in OBS_KEYS}
                    ma = {k: buf.act[k][ti, ri].flatten(0, 1) for k in act_keys}
                    if K > 1:
                        live = buf.valid[ti, ri].flatten().float()
                        mean = lambda x: (x * live).sum() / live.sum().clamp(min=1)
                    else:
                        mean = torch.mean
                    if args.memory:
                        pout = policy.forward_seq(mo, buf.state[start_t[part], chunk_row[part]], buf.done[ti, ri], K)
                    else:
                        pout, _ = policy(mo)
                    lp = log_prob(pout, ma)
                    logratio = lp - buf.logp[ti, ri].flatten()
                    ratio = logratio.exp()
                    madv = adv[ti, ri].flatten()
                    pg = -mean(torch.min(ratio * madv, ratio.clamp(1 - args.clip, 1 + args.clip) * madv))
                    vf = mean((pout["value"] - ret[ti, ri].flatten()).pow(2))
                    ent = mean(entropy(pout))
                    loss = pg + args.vf * vf - args.entropy * ent
                    (loss * w).backward()
                    with torch.no_grad():
                        kl += w * mean((ratio - 1) - logratio).item()
                        lead_logratio = lead_log_prob(pout, ma) - buf.lead_logp[ti, ri].flatten()
                        rest_logratio = logratio - lead_logratio
                        kl_lead += w * mean(lead_logratio.exp() - 1 - lead_logratio).item()
                        kl_rest += w * mean(rest_logratio.exp() - 1 - rest_logratio).item()
                        sums["pg"] += w * pg.item()
                        sums["vf"] += w * vf.item()
                        sums["ent"] += w * ent.item()
                        sums["clip"] += w * mean(((ratio - 1).abs() > args.clip).float()).item()
                torch.nn.utils.clip_grad_norm_(policy.parameters(), args.grad_clip)
                opt.step()
                if args.free_lead:
                    with torch.no_grad():
                        policy.lead_logstd.clamp_(*LEAD_LOGSTD)
                sums["kl"] += kl
                sums["kl_lead"] += kl_lead
                updates += 1
                if args.target_kl and (kl_rest if args.free_lead else kl) > args.target_kl:
                    stopped = True
                    break
        train_time = time.time() - t1

        buf.shift(T, L)
        pos = L

        row = {"iter": it, "steps": total_steps, "sps": round(T * rows / collect_time),
               "collect_s": round(collect_time, 2), "train_s": round(train_time, 2), "dropped_credits": dropped,
               "time": {k: round(v, 2) for k, v in timers.items()},
               "lead_std": policy.lead_logstd.exp().item(), "mean_reward": mean_reward, "updates": updates,
               "gpu_mb": torch.cuda.max_memory_reserved() // 2**20 if DEVICE == "cuda" else 0}
        row.update({k: round(v / updates, 4) for k, v in sums.items()})
        if args.spawn_ramp:
            row["spawn"] = [round(x) for x in spawn_range(args, it)]
        if args.shapes:
            sa = shape_args(args, it)
            row["shapes"] = [round(sa["shapes_min"], 3), round(sa["shapes_max"], 3)]
        row["screen"] = {"mean": screen["sum"] / max(screen["n"], 1), "max": screen["max"],
                         "cut": screen["cut"] / max(screen["n"], 1)}
        writer.add_scalar("screen/max", screen["max"], it)
        writer.add_scalar("screen/cut", row["screen"]["cut"], it)
        if drones:
            ctrl = dict(zip(CTRL, ctrl.tolist()))
            n = max(ctrl["n"], 1)
            # Mean mouse distance while clicking: as a multiple of the target's, and free.
            row["mouse"] = {"attract": ctrl["attract"] / n, "repel": ctrl["repel"] / n,
                            "placed": ctrl["placed"] / n, "free": ctrl["free"] / n,
                            "ratio": float(np.exp(ctrl["placed_reach"] / max(ctrl["placed"], 1))),
                            "free_reach": float(np.exp(ctrl["free_reach"] / max(ctrl["free"], 1))),
                            "ratio_std": policy.reach_logstd.exp().item(),
                            "free_std": policy.free_reach_logstd.exp().item()}
            for k, v in row["mouse"].items():
                writer.add_scalar(f"mouse/{k}", v, it)
        if pool:
            row["pool"] = {"size": len(pool.snapshots), "picked": picked}
        if rival:
            row["rival_pool"] = {"size": len(rival.snapshots) + 1, "picked": rival_picked}
        if len(ffa_games):
            ep, games = max(ffa["ep"], 1), max(ffa["games"], 1)
            # Per seat: last one left, dead, alive at the timeout (and of those, never having seen a tank still
            # alive), kills, and the kills it had done under a quarter of the damage for. Per game: timed out,
            # no kill at all, and the decision of the first kill.
            row["ffa"] = {"episodes": ffa["ep"], "last": ffa["won"] / ep, "died": ffa["lost"] / ep,
                          "survived": ffa["timeout"] / ep, "apart": ffa["apart"] / max(ffa["timeout"], 1),
                          "kills": ffa["kills"] / ep, "stolen": ffa["stolen"] / max(ffa["kills"], 1),
                          "alive": ffa["alive"] / max(ffa["decisions"], 1), "games": ffa["games"],
                          "timed_out": ffa["timed_out"] / games, "no_kill": ffa["no_kill"] / games,
                          "first_kill": float(np.median(ffa["first_kill"])) if ffa["first_kill"] else None}
            for k, v in row["ffa"].items():
                if v is not None:
                    writer.add_scalar(f"ffa/{k}", v, it)
        if team_row.any():
            games, pool_games = max(team["games"], 1), max(team["pool_games"], 1)
            row["team"] = {"games": team["games"], "draw": team["timeouts"] / games, "pool_games": team["pool_games"],
                           "win": team["pool_won"] / pool_games, "loss": team["pool_lost"] / pool_games,
                           "whole": team["whole"] / max(team["wins"], 1),
                           "together": team["together"] / max(team["with_mate"], 1),
                           "assists": team["assists"] / max(team["kills"], 1),
                           "same_target": team["same"] / max(team["targeted"], 1)}
            for k, v in row["team"].items():
                writer.add_scalar(f"team/{k}", v, it)
        msg = [f"it {it}", f"{row['sps']} sps", f"ent {row['ent']:.2f}",
               f"kl {row['kl']:.3f} lead {row['kl_lead']:.3f}", f"vf {row['vf']:.3f}",
               f"screen max {screen['max']}" + (f" cut {row['screen']['cut']:.1%}" if screen["cut"] else "")]
        if "ffa" in row:
            f = row["ffa"]
            msg.append(f"ffa last {f['last']:.0%} died {f['died']:.0%} kills {f['kills']:.2f} timed out {f['timed_out']:.0%}")
        if "team" in row:
            f = row["team"]
            msg.append(f"team vs pool win {f['win']:.0%} loss {f['loss']:.0%}  draw {f['draw']:.0%} whole {f['whole']:.0%} "
                       f"together {f['together']:.0%} assists {f['assists']:.0%} same target {f['same_target']:.0%}")
        for kind, s in stats.items():
            ep = max(s["ep"], 1)
            row[kind] = {"episodes": s["ep"], "win": s["won"] / ep, "loss": s["lost"] / ep, "draw": s["timeout"] / ep,
                         "hit_rate": s["hits"] / max(s["shots"], 1), "dealt": s["dealt"] / ep, "taken": s["taken"] / ep,
                         "met": s["met"] / ep, "first_seen": s["first_seen"] / max(s["met"], 1)}
            if args.shapes:
                # Damage from shapes per game, the share of shots a shape stopped, and deaths to shapes per game.
                row[kind].update(shape_taken=s["shape_taken"] / ep, shape_blocked=s["shape_blocked"] / max(s["shots"], 1),
                                 died_to_shape=s["died_to_shape"] / ep)
            for k in ("win", "loss", "hit_rate", "dealt", "taken") + (("shape_taken", "died_to_shape") if args.shapes else ()):
                writer.add_scalar(f"{kind}/{k}", row[kind][k], it)
            msg.append(f"{kind}: n {s['ep']} win {row[kind]['win']:.0%} loss {row[kind]['loss']:.0%} "
                       f"hit {row[kind]['hit_rate']:.0%}" + (f" met {row[kind]['met']:.0%}" if s["met"] < s["ep"] else ""))
            if args.shapes and kind == "live":
                msg.append(f"shapes {row['shapes'][1]:g}: taken {row[kind]['shape_taken']:.2f} "
                           f"blocked {row[kind]['shape_blocked']:.0%} died {row[kind]['died_to_shape']:.1%}")
        if shared:
            # As each class, against all; and the focus class against each.
            played, focus_vs = {}, {}
            for (a_t, b_t), m in matchups.items():
                for table, key in ((played, a_t), (focus_vs, b_t if a_t == focus else None)):
                    if key is None:
                        continue
                    t = table.setdefault(key, {"ep": 0, "won": 0, "lost": 0})
                    for k in t:
                        t[k] += m[k]

            def rates(table):
                return {TANK_NAMES[t]: {"episodes": m["ep"], "win": m["won"] / m["ep"], "loss": m["lost"] / m["ep"],
                                        "draw": 1 - (m["won"] + m["lost"]) / m["ep"]} for t, m in table.items()}

            row["as"], row["focus_vs"] = rates(played), rates(focus_vs)
            for t, m in focus_vs.items():
                if t in pfsp and m["ep"] >= 4:
                    new = {"loss": m["lost"] / m["ep"], "draw": 1 - (m["won"] + m["lost"]) / m["ep"]}
                    old = pfsp[t]
                    pfsp[t] = new if old is None else {k: 0.8 * old[k] + 0.2 * new[k] for k in new}
            if args.pfsp:
                row["pfsp"] = {TANK_NAMES[t]: round(w, 3) for t, w in pfsp_weights(pfsp, opp_classes).items()}
            msg.append(f"{TANK_NAMES[focus]} vs " + " ".join(
                f"{n} {r['win']:.0%}/{r['loss']:.0%}" for n, r in sorted(row["focus_vs"].items())))
        for k in ("pg", "vf", "ent", "kl", "kl_lead", "clip", "sps", "lead_std", "mean_reward", "updates"):
            writer.add_scalar(f"ppo/{k}", row[k], it)
        if drones:
            m = row["mouse"]
            msg.append(f"mouse: attract {m['attract']:.0%} repel {m['repel']:.0%} ratio {m['ratio']:.2f} "
                       f"free {m['free_reach']:.0f}")
        print("  ".join(msg), flush=True)

        ckpt = {"model": policy.state_dict(), "model_args": model_args, "env_args": env_args, "iter": it}
        # Written whole or not at all: a rival may be reading these.
        save_atomic(ckpt, os.path.join(out, "latest.pt"))
        # The exams below play at once, each group by itself from the same seed, then report in the order listed.
        exams = []

        def exam(config, episodes, report, opponent=None, blind=(), group=None):
            exams.append(({"config": config, "episodes": episodes, "opponent": opponent, "blind": blind}, report,
                          group))

        evals = it % args.eval_every == 0 or it == args.iters
        if evals and args.eval_episodes:
            def report(r):
                print(f"eval {it} vs {eval_opponent}: {describe(r)}", flush=True)
                row["eval"] = {"opponent": eval_opponent, "win": r["win"], "loss": r["loss"][0],
                               "hit_rate": r["hit_rate"], "episodes": r["episodes"]}
                writer.add_scalar(f"eval/{eval_opponent}_win", r["win"][0], it)
                writer.add_scalar(f"eval/{eval_opponent}_hit", r["hit_rate"], it)
            exam(make_config(eval_opponent, **anchor_args(args, env_args)), args.eval_episodes, report)
        if evals and reference is not None:
            def report(r):
                print(f"eval {it} vs {args.eval_vs}: {describe(r)}", flush=True)
                row["eval_vs"] = {"opponent": args.eval_vs, "win": r["win"], "loss": r["loss"][0],
                                  "draw": r["timed_out"] / max(r["episodes"], 1), "hit_rate": r["hit_rate"],
                                  "episodes": r["episodes"]}
                writer.add_scalar("eval/vs_reference_win", r["win"][0], it)
                writer.add_scalar("eval/vs_reference_loss", r["loss"][0], it)
            exam(make_config("self", **vs_args), args.eval_episodes, report, opponent=reference)
        if evals:
            save_atomic(ckpt, os.path.join(out, f"it{it:05d}.pt"))
        checks = it % args.frozen_eval_every == 0 or it == args.iters
        if mirror and checks:
            row["mirror_eval"] = {}
            for b in mirror:
                def report(r, b=b):
                    print(f"eval {it} as {TANK_NAMES[b['tank']]} vs {b['label']}: {describe(r)}", flush=True)
                    row["mirror_eval"][b["label"]] = {"win": r["win"][0], "loss": r["loss"][0],
                                                      "draw": r["timed_out"] / max(r["episodes"], 1),
                                                      "episodes": r["episodes"]}
                    writer.add_scalar(f"mirror_eval/{b['label']}", r["win"][0], it)
                bot_args = dict(anchor_args(args, env_args), tanks=[b["tank"]] * 2, stats=[b["stats"]] * 2)
                exam(make_config("self", **bot_args), args.frozen_eval_episodes, report, opponent=b["policy"])
        if sentinels and checks:
            # The focus class against the policy playing each sentinel class under every condition, and at the end
            # each other class too without the blind one. Each condition plays the same games.
            conditions = exam_conditions(args)
            plan = [(t, list(conditions)) for t, _ in sentinels]
            if it == args.iters:
                named = {t for t, _ in sentinels}
                plan += [(t, ["plain", "shapes"]) for t, _ in opp_classes if t not in named]
            row["field_exam"] = {}
            for t, names in plan:
                field_args = dict(env_args, tanks=[focus, t], stats=[builds[focus], builds.get(t, build_for(t))])
                row["field_exam"][TANK_NAMES[t]] = {}
                for cond in names:
                    def report(r, t=t, cond=cond):
                        print(f"eval {it} {TANK_NAMES[focus]} vs {TANK_NAMES[t]} {cond}: {describe(r)}", flush=True)
                        row["field_exam"][TANK_NAMES[t]][cond] = {"win": r["win"][0], "loss": r["loss"][0],
                                                                  "draw": r["timed_out"] / max(r["episodes"], 1),
                                                                  "episodes": r["episodes"], **shape_results(r)}
                        writer.add_scalar(f"field_exam/{TANK_NAMES[t]}_{cond}", r["win"][0], it)
                    extra, blind = conditions[cond]
                    exam(make_config("self", **field_args, **extra), args.frozen_eval_episodes, report,
                         opponent=policy, blind=blind, group=cond)
        if shared and checks and not sentinels:
            # The focus class against the policy playing each opponent class, best moves on both sides.
            row["field_eval"] = {}
            for t, _ in opp_classes:
                def report(r, t=t):
                    print(f"eval {it} {TANK_NAMES[focus]} vs {TANK_NAMES[t]}: {describe(r)}", flush=True)
                    row["field_eval"][TANK_NAMES[t]] = {"win": r["win"][0], "loss": r["loss"][0],
                                                        "draw": r["timed_out"] / max(r["episodes"], 1)}
                field_args = dict(env_args, tanks=[focus, t], stats=[builds[focus], builds[t]])
                exam(make_config("self", **field_args), args.frozen_eval_episodes, report, opponent=policy)
        if shared and args.shapes and checks and not sentinels:
            # The same with shapes.
            row["field_shapes_eval"] = {}
            for t, _ in opp_classes:
                def report(r, t=t):
                    print(f"eval {it} {TANK_NAMES[focus]} vs {TANK_NAMES[t]} with shapes: {describe(r)}", flush=True)
                    row["field_shapes_eval"][TANK_NAMES[t]] = {"win": r["win"][0], "loss": r["loss"][0],
                                                               "draw": r["timed_out"] / max(r["episodes"], 1),
                                                               **shape_results(r)}
                field_args = dict(env_args, tanks=[focus, t], stats=[builds[focus], builds[t]], **exam_shapes(args))
                exam(make_config("self", **field_args), args.frozen_eval_episodes, report, opponent=policy)
        if shape_vs and checks:
            # The gap between the two is what shapes cost, or what using them gains.
            row["shape_eval"] = {}
            for b in shape_vs:
                bot_args = versus(anchor_args(args, env_args), b["tank"], b["stats"])
                row["shape_eval"][b["label"]] = {}
                for label, extra in (("shapes", exam_shapes(args)), ("none", {})):
                    def report(r, b=b, label=label, extra=extra):
                        print(f"eval {it} vs {b['label']} {'with' if extra else 'without'} shapes: {describe(r)}",
                              flush=True)
                        row["shape_eval"][b["label"]][label] = {"win": r["win"][0], "loss": r["loss"][0],
                                                                "draw": r["timed_out"] / max(r["episodes"], 1),
                                                                "episodes": r["episodes"], **shape_results(r)}
                        writer.add_scalar(f"shape_eval/{b['label']}_{label}", r["win"][0], it)
                    exam(make_config("self", **bot_args, **extra), args.frozen_eval_episodes, report,
                         opponent=b["policy"])
        if (frozen or holdout) and checks:
            row["frozen_eval"] = {}
            for b in frozen + holdout:
                def report(r, b=b):
                    trained = "" if b in frozen else " (held out)"
                    print(f"eval {it} vs {b['label']}{trained}: {describe(r)}", flush=True)
                    row["frozen_eval"][b["label"]] = {"held_out": b in holdout, "win": r["win"][0],
                                                      "loss": r["loss"][0],
                                                      "draw": r["timed_out"] / max(r["episodes"], 1),
                                                      "episodes": r["episodes"]}
                    writer.add_scalar(f"frozen_eval/{b['label']}", r["win"][0], it)
                bot_args = versus(anchor_args(args, env_args), b["tank"], b["stats"])
                exam(make_config("self", **bot_args), args.frozen_eval_episodes, report, opponent=b["policy"])
        results = {}
        for group in dict.fromkeys(g for _, _, g in exams):
            picked = [i for i, e in enumerate(exams) if e[2] == group]
            got = evaluate_many(policy, [exams[i][0] for i in picked], threads=args.threads, seed=1000 + it)
            results.update(zip(picked, got))
        for i, (_, report, _) in enumerate(exams):
            report(results[i])
        if rival and checks:
            row["rival_eval"] = {}
            newest = rival.snapshots[-1] if rival.snapshots else (0, rival.anchor)
            for label, snap in (("anchor", rival.anchor), (f"newest it{newest[0]}", newest[1])):
                rival.policy.load_state_dict(snap)
                r = evaluate(policy, make_config("self", **versus(env_args, rival.tank, rival.stats)),
                             args.frozen_eval_episodes, threads=args.threads, seed=4000 + it, opponent=rival.policy)
                print(f"eval {it} vs rival {label}: {describe(r)}", flush=True)
                row["rival_eval"][label.split()[0]] = {"snapshot": label, "win": r["win"][0], "loss": r["loss"][0],
                                                       "draw": r["timed_out"] / max(r["episodes"], 1),
                                                       "met": r["met"] / max(r["episodes"], 1),
                                                       "episodes": r["episodes"]}
                writer.add_scalar(f"rival_eval/{label.split()[0]}", r["win"][0], it)
        if ffa_vs is not None and checks:
            r = evaluate_ffa(policy, ffa_exam_config(), args.ffa_exam_episodes, threads=args.threads, seed=8000 + it,
                             opponent=ffa_vs)
            print(f"eval {it} free-for-all vs {name(args.ffa_vs)}: {describe_ffa(r)}", flush=True)
            n = max(r["episodes"], 1)
            row["ffa_eval"] = {"opponent": args.ffa_vs, "last": r["win"][0], "died": r["lost"] / n,
                               "survived": r["timed_out"] / n, "kills": r["kills"] / n, "episodes": r["episodes"]}
            writer.add_scalar("ffa_eval/last", r["win"][0], it)
            writer.add_scalar("ffa_eval/kills", r["kills"] / n, it)
        if team_vs is not None and (it % args.team_exam_every == 0 or it == args.iters):
            r = evaluate_team(policy, team_exam_config(), args.team_exam_episodes, threads=args.threads,
                              seed=9000 + it, opponent=team_vs)
            print(f"eval {it} teams vs {name(args.team_vs)}: {describe_team(r)}", flush=True)
            n = max(r["episodes"], 1)
            row["team_eval"] = {"opponent": args.team_vs, "win": r["win"][0], "loss": r["lost"] / n,
                                "draw": r["timed_out"] / n, "whole": r["whole"] / max(r["won"], 1),
                                "kills": r["kills"] / n, "episodes": r["episodes"]}
            writer.add_scalar("team_eval/win", r["win"][0], it)
            writer.add_scalar("team_eval/loss", r["lost"] / n, it)
        log.write(json.dumps(row) + "\n")
        log.flush()
        if it % args.save_every == 0 or it == args.iters or stopping:
            save_atomic(resume_state(it), resume_path)
        if stopping:
            print(f"stopped after iteration {it}; continue with --resume", flush=True)
            raise SystemExit(128 + stopping[0])


if __name__ == "__main__":
    main()
