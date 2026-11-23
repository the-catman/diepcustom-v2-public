"""Records duels into a self-contained HTML replay that plays through the game's own client, and
optionally an mp4 of the whole arena.

    python watch.py runs/x/latest.pt --opponent fighter --episodes 5 --out replay.html --video replay.mp4
    python watch.py --scripted fighter --opponent strafe --out replay.html
    python watch.py runs/x/latest.pt --players 4 --arena 6000 --remember --out ffa.html

With --players past 2 it's a free-for-all: the checkpoint in the first seat and --opponent, by default the
checkpoint too, in the rest, each seat dealt a class at random unless --tank or --opp-tank name one.
--seats names every seat's class in turn, and the number of tanks with it:

    python watch.py runs/x/latest.pt --seats penta-shot,fighter,overlord,factory --remember --out ffa.html

With --teams the seats split into two teams, blue's first: the checkpoint plays blue's team and --opponent the other.

    python watch.py runs/x/latest.pt --teams --seats fighter,overlord,penta-shot,factory --remember --out teams.html

--classic writes the old schematic viewer instead, which also shows where drone tanks put their mouse.
"""
import argparse
import json
import os

import torch

from common import diepenv, make_config, tank_id, tanks_of, builds_of, build_for, parse_classes, KINDS, MOUSE_DRONES, \
    TANK_NAMES
from evaluate import act, forget, load, pick_classes, Actions, DEVICE, OBS_KEYS
import replay

VIEWER = os.path.join(os.path.dirname(__file__), "viewer.html")


def name(path):
    """runs/x/it00500.pt -> x/it00500"""
    if path in KINDS:
        return path
    return os.path.join(os.path.basename(os.path.dirname(os.path.abspath(path))), os.path.splitext(os.path.basename(path))[0])


def run_classes(path):
    """The classes the checkpoint's run dealt its tank, from the run's settings; none if it had just one."""
    settings = os.path.join(os.path.dirname(os.path.abspath(path)), "args.json")
    if not os.path.exists(settings):
        return []
    with open(settings) as f:
        classes = json.load(f).get("classes")
    return [t for t, _ in parse_classes(classes)] if classes else []


def main():
    p = argparse.ArgumentParser()
    p.add_argument("checkpoint", nargs="?")
    p.add_argument("--scripted", choices=[k for k in KINDS if k != "self"], help="watch a scripted tank instead")
    p.add_argument("--opponent", default=None,
                   help="a scripted opponent, or a checkpoint; fighter in a duel, the checkpoint in a free-for-all")
    p.add_argument("--episodes", type=int, default=5)
    p.add_argument("--stochastic", action="store_true")
    p.add_argument("--seed", type=int, default=0)
    p.add_argument("--aim-noise", type=float, default=0.0)
    p.add_argument("--tank", default=None, help="blue's class, for --scripted or an any-class checkpoint; with --teams "
                                                "blue's team's")
    p.add_argument("--opp-tank", default=None, help="red's class, or every other tank's")
    p.add_argument("--shapes", type=float, default=0.0, help="shapes, as a multiple of FFA's density")
    p.add_argument("--out", default="replay.html")
    p.add_argument("--video", default=None, help="also render the whole arena into this mp4")
    p.add_argument("--video-size", type=int, default=1440, help="the video's width and height")
    p.add_argument("--classic", action="store_true", help="the old schematic viewer, without the client")
    p.add_argument("--players", type=int, default=2)
    p.add_argument("--arena", type=float, default=None, help="arena size, default the checkpoint's")
    p.add_argument("--decisions", type=int, default=None, help="longest a game runs, default the checkpoint's")
    p.add_argument("--spawn", type=float, default=None,
                   help="closest two free-for-all tanks start, default 1500; or two teams, default 2000")
    p.add_argument("--teams", action="store_true", help="two teams of half the seats each, blue's first")
    p.add_argument("--kill-reward", type=float, default=None)
    p.add_argument("--remember", action="store_true",
                   help="show the checkpoint's seats enemy tanks off screen where they were last seen")
    p.add_argument("--seats", default=None, help="each free-for-all seat's class in turn, blue first")
    args = p.parse_args()
    seat_tanks = [tank_id(c) for c in args.seats.split(",")] if args.seats else []
    if seat_tanks:
        if len(seat_tanks) < 3:
            p.error("--seats is for free-for-alls, of 3 tanks or more")
        args.players = len(seat_tanks)
    ffa = args.players > 2
    if args.teams and (args.players < 4 or args.players % 2):
        p.error("--teams needs an even number of tanks, 4 or more")
    # The seats the checkpoint plays: its team's, or just blue.
    own = args.players // 2 if args.teams else 1
    if ffa and args.classic:
        p.error("the schematic viewer only draws duels")
    args.opponent = args.opponent or (args.checkpoint if ffa else "fighter")
    if args.classic and args.video:
        p.error("--video plays through the client, so it can't go with --classic")
    if not args.checkpoint and not args.scripted:
        p.error("give a checkpoint or --scripted")

    policy, env_args = None, {}
    if args.checkpoint:
        policy, env_args = load(args.checkpoint)
    env_args["aim_noise"] = args.aim_noise
    tanks, stats = tanks_of(env_args), builds_of(env_args)
    rival = None
    if args.opponent in KINDS:
        kind = args.opponent
    else:
        rival, rival_env = load(args.opponent)
        tanks[1], stats[1] = tanks_of(rival_env)[0], builds_of(rival_env)[0]
        kind = "self"
    if args.tank:
        pick_classes(tanks, stats, 0, args.tank, [policy])
    if args.opp_tank:
        pick_classes(tanks, stats, 1, args.opp_tank, [rival if rival is not None else policy])
    env_args["tanks"], env_args["stats"] = tanks, stats
    env_args.update(shapes_min=args.shapes, shapes_max=args.shapes)
    for k, v in (("arena_size", args.arena), ("max_decisions", args.decisions), ("kill_reward", args.kill_reward)):
        if v is not None:
            env_args[k] = v
    config = make_config(kind, **env_args)
    if args.scripted:
        config.kinds = [KINDS[args.scripted], config.kinds[1]]
        config.expose_scripted = True
    n = args.players
    # The checkpoint's seats, and the opponent's when it plays itself.
    mine = [True] * own + [rival is None and kind == "self"] * (n - own)
    if ffa:
        config.players = n
        config.kinds = [config.kinds[0]] * own + [config.kinds[1]] * (n - own)
        config.spawn_min, config.mirror_spawn = args.spawn or (2000 if args.teams else 1500), False
        if args.teams:
            config.teams = [0] * own + [1] * own
            config.spawn_max = max(4500, config.spawn_min)
            config.loss_reward, config.eliminated_reward = 0.5, 0.5
        deck = [(t, list(build_for(t)), 1.0) for t in run_classes(args.checkpoint)] or [(tanks[1], list(stats[1]), 1.0)]
        seats = [[(tanks[0], list(stats[0]), 1.0)] if args.tank else deck] * own + \
                [[(tanks[1], list(stats[1]), 1.0)] if args.opp_tank else deck] * (n - own)
        if seat_tanks:
            seats = [[(t, list(build_for(t)), 1.0)] for t in seat_tanks]
        if any(getattr(q, "any_class", False) for q in (policy, rival) if q is not None):
            config.choices = seats
        elif seat_tanks:
            p.error("--seats needs an any-class checkpoint")
        else:
            config.tanks, config.stats = [tanks[0]] * own + [tanks[1]] * (n - own), [stats[0]] * own + [stats[1]] * (n - own)
    config.remember = [args.remember and m for m in mine]
    vec = diepenv.Vec(config, 1, 1)
    vec.seed(args.seed)
    vec.reset_all()
    barrels = [vec.duel_barrels(0, s) for s in range(n)]
    blue_name = args.scripted or name(args.checkpoint)
    recording = None
    if not args.classic:
        recording = replay.Recording()
        vec.duel_start_replay(0, replay.views(config.arena_size), [blue_name] * own + [name(args.opponent)] * (n - own))

    episodes, frames, results = 0, [], []
    obs = vec.observe()
    n = vec.rows
    # Each policy drives the row of its own slot.
    drivers = [(policy, [r for r, (_, s) in enumerate(vec.row_map) if s < own])]
    others = [r for r, (_, s) in enumerate(vec.row_map) if s >= own]
    if rival is not None:
        drivers.append((rival, others))
    elif kind == "self":
        drivers.append((policy, others))
    states = [p.initial_state(len(rows), DEVICE) if p is not None else None for p, rows in drivers]
    # Each tank's mouse, for drone classes: x, y, 1 attract / -1 repel / 0 neither.
    def mice():
        out = []
        for t in vec.duel_state(0)["tanks"]:
            click = 0 if not t else -1 if t["repel"] else 1 if t["attract"] else 0
            out.append([round(t["mouse_x"], 1), round(t["mouse_y"], 1), click] if t else None)
        return out

    drones = [t in MOUSE_DRONES for t in tanks]
    outcome = None
    while episodes < args.episodes:
        frame = {"e": episodes, "d": vec.duel_entities(0).round(2).tolist()}
        if any(drones):
            frame["m"] = [m if d else None for m, d in zip(mice(), drones)]
        frames.append(frame)
        a = Actions(n)
        for i, (p, rows) in enumerate(drivers):
            if p is None:
                continue
            got, states[i] = act(p, {k: obs[k][rows] for k in OBS_KEYS}, not args.stochastic, states[i])
            a.set(rows, got)
        obs = a.step(vec)
        if recording:
            recording.add(vec.duel_take_packets(0))
        for i, (_, rows) in enumerate(drivers):
            forget(states[i], obs["done"][rows])
        if args.teams:
            if obs["game_over"][0]:
                results.append("blue's team won" if obs["team_won"][0] else "blue's team lost" if obs["team_lost"][0]
                               else "both teams lived out the clock")
                episodes += 1
        elif ffa:
            if obs["done"][0]:
                outcome = "blue was the last left" if obs["won"][0] else "blue died" if obs["lost"][0] else "blue lived out the clock"
            if obs["game_over"][0]:
                results.append(outcome)
                episodes += 1
        elif obs["done"][0]:
            results.append("win" if obs["won"][0] else "loss" if obs["lost"][0] else "draw")
            episodes += 1

    blue, red = (TANK_NAMES[t] for t in tanks)
    title = f"{blue_name} ({blue}, blue) vs {name(args.opponent)} ({red}, red)"
    if ffa:
        title = f"{blue_name} (blue) in a {n}-tank free-for-all vs {name(args.opponent)}"
    if args.teams:
        title = f"{blue_name} (blue's team) in a {own}v{own} vs {name(args.opponent)}"
    if recording:
        page = replay.html(recording, title, results, config.arena_size)
    else:
        data = {"arena": config.arena_size, "frames": frames, "results": results, "barrels": barrels, "title": title}
        with open(VIEWER) as f:
            page = f.read().replace("/*DATA*/null", json.dumps(data, separators=(",", ":")))
    with open(args.out, "w") as f:
        f.write(page)
    print(f"{args.out}: {len(frames)} decisions, {' '.join(results)}")
    if args.video:
        shot = replay.video(args.out, args.video, len(results), size=args.video_size)
        print(f"{args.video}: {shot} frames")


if __name__ == "__main__":
    main()
