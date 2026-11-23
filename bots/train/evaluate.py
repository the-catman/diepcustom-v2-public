import argparse
import math

import numpy as np
import torch

from common import diepenv, make_config, tank_id, tanks_of, builds_of, build_for, KINDS
from model import Policy, sample, env_action

DEVICE = "cuda" if torch.cuda.is_available() else "cpu"
OBS_KEYS = ("self", "ents", "mask", "targetable", "tank")
# The kind flag of the rows each blind exam hides.
HIDDEN = {"shapes": diepenv.CLASS_ENT_DIM}


def wilson(k, n, z=1.96):
    if n == 0:
        return 0.0, 0.0, 1.0
    p = k / n
    centre = (p + z * z / (2 * n)) / (1 + z * z / n)
    half = z * math.sqrt(p * (1 - p) / n + z * z / (4 * n * n)) / (1 + z * z / n)
    return p, max(0.0, centre - half), min(1.0, centre + half)


def to_device(obs):
    return {k: torch.from_numpy(obs[k]).to(DEVICE) for k in OBS_KEYS}


def hide(obs, blind):
    """obs without the rows of the kinds in blind, the rest moved up; and the row each row now shown was."""
    keep = obs["mask"].copy()
    for kind in blind:
        keep &= obs["ents"][..., HIDDEN[kind]] != 1
    order = np.argsort(~keep, axis=1, kind="stable")
    out = dict(obs)
    out["mask"] = np.take_along_axis(keep, order, 1)
    out["targetable"] = np.take_along_axis(obs["targetable"], order, 1) & out["mask"]
    out["ents"] = np.take_along_axis(obs["ents"], order[..., None], 1)
    return out, order


@torch.no_grad()
def act(policy, obs, deterministic, state=None, blind=()):
    if blind:
        obs, order = hide(obs, blind)
    out, state = policy(to_device(obs), state)
    a = {k: v.cpu().numpy() for k, v in env_action(sample(out, deterministic)).items()}
    if blind:
        aimed = np.flatnonzero(a["target"] >= 0)
        a["target"][aimed] = order[aimed, a["target"][aimed]]
    return a, state


class Actions:
    """Every row's action for Vec.step. Rows driven without drone controls never right click,
    and keep their mouse far away."""

    def __init__(self, rows):
        self.a = {"move": np.zeros(rows, np.int32), "fire": np.zeros(rows, bool), "target": np.full(rows, -1, np.int32),
                  "aim": np.zeros(rows, np.float32), "repel": np.zeros(rows, bool),
                  "reach": np.full(rows, np.nan, np.float32)}

    def set(self, rows, act):
        for k, v in act.items():
            self.a[k][rows] = v

    def step(self, vec):
        return vec.step(**self.a)


def forget(state, done):
    """Clears the memory of rows whose episode just ended."""
    if state is not None and done.any():
        state[torch.from_numpy(done).to(state.device)] = 0


def load(path):
    ckpt = torch.load(path, map_location=DEVICE)
    policy = Policy(**ckpt.get("model_args", {})).to(DEVICE)
    policy.load_state_dict(ckpt["model"])
    return policy, dict(ckpt.get("env_args", {}))


def pick_classes(tanks, stats, slot, cls, policies):
    """Sets a slot's class; one any-class policy among policies plays it on that class's usual build."""
    tanks[slot] = tank_id(cls)
    if any(getattr(p, "any_class", False) for p in policies):
        stats[slot] = build_for(tanks[slot])


TOTALS = ("won", "lost", "timed_out", "episodes", "dealt", "taken", "shots", "hits", "decisions", "met", "first_seen",
          "shape_taken", "shape_blocked", "died_to_shape")
COUNTS = {"won", "lost", "timed_out", "episodes", "shots", "hits", "decisions", "met", "first_seen", "shape_blocked",
          "died_to_shape"}


def evaluate(policy, config, episodes=400, duels=200, threads=8, seed=12345, deterministic=True, opponent=None,
             blind=()):
    """Plays each duel ceil(episodes / duels) times, so short episodes aren't over-counted.

    With an opponent policy, both tanks are policies and the results are the first one's. blind names kinds of
    thing, so far only "shapes", the first policy is never shown.
    """
    job = {"config": config, "episodes": episodes, "duels": duels, "opponent": opponent, "blind": blind}
    return evaluate_many(policy, [job], threads, seed, deterministic)[0]


def evaluate_many(policy, jobs, threads=8, seed=12345, deterministic=True, max_duels=1200):
    """evaluate for each job, a dict of its arguments: config, and if not the defaults episodes, duels, opponent
    and blind. Returns each job's results, in order.

    The jobs play side by side, as many at a time as fit in max_duels duels, and a duel that has played its share
    sits out the rest, so a long game holds up only its own duel.
    """
    jobs = [{"episodes": 400, "duels": 200, "opponent": None, "blind": (), **j} for j in jobs]
    results, batch = [], []
    for j in jobs:
        if batch and sum(b["duels"] for b in batch) + j["duels"] > max_duels:
            results += _play(policy, batch, threads, seed + len(results), deterministic)
            batch = []
        batch.append(j)
    return results + (_play(policy, batch, threads, seed + len(results), deterministic) if batch else [])


def _play(policy, jobs, threads, seed, deterministic):
    configs, quota, job_of = [], [], []
    for i, j in enumerate(jobs):
        config, opponent = j["config"], j["opponent"]
        if opponent is not None:
            config.kinds = [diepenv.EXTERNAL, diepenv.EXTERNAL]
        if opponent is policy:
            config.remember = [config.remember[0]] * 2
        configs += [config] * j["duels"]
        quota += [math.ceil(j["episodes"] / j["duels"])] * j["duels"]
        job_of += [i] * j["duels"]
    vec = diepenv.Vec(configs, threads)
    vec.seed(seed)
    vec.reset_all()
    left, job_of = np.array(quota), np.array(job_of)
    duel = np.array([d for d, _ in vec.row_map])
    slot = np.array([s for _, s in vec.row_map])
    # The policy drives its own seats and each job's opponent the other; a policy's rows shown the same go together.
    grouped = {}
    for r in range(vec.rows):
        j = jobs[job_of[duel[r]]]
        p, blind = (policy, tuple(j["blind"])) if slot[r] == 0 else (j["opponent"], ())
        grouped.setdefault((id(p), blind), (p, blind, []))[2].append(r)
    drivers = [(p, blind, np.array(rows), p.initial_state(len(rows), DEVICE)) for p, blind, rows in grouped.values()]
    mine = np.flatnonzero(slot == 0)
    mine_duel, mine_job = duel[mine], job_of[duel[mine]]
    sums = {k: np.zeros(len(jobs)) for k in TOTALS}

    def add(key, values, where):
        sums[key] += np.bincount(mine_job[where], weights=values[where], minlength=len(jobs))

    full = vec.observe()
    actions = Actions(vec.rows)
    while (left > 0).any():
        playing = left > 0
        vec.set_paused(~playing)
        for p, blind, rows, state in drivers:
            on = np.flatnonzero(playing[duel[rows]])
            if not len(on):
                continue
            at = torch.from_numpy(on).to(DEVICE)
            a, after = act(p, {k: full[k][rows[on]] for k in OBS_KEYS}, deterministic,
                           None if state is None else state[at], blind)
            if state is not None:
                state[at] = after
            actions.set(rows[on], a)
        full = actions.step(vec)
        for p, blind, rows, state in drivers:
            forget(state, full["done"][rows])
        obs = {k: full[k][mine] for k in ("done", "won", "lost", "timed_out", "dealt", "taken", "shots", "hits",
                                          "shape_taken", "shape_blocked", "died_to_shape", "first_seen")}
        live = playing[mine_duel]
        ended = obs["done"] & live
        for k in ("dealt", "taken", "shots", "hits", "shape_taken", "shape_blocked"):
            add(k, obs[k].astype(np.float64), live)
        add("decisions", np.ones(len(mine)), live)
        for k in ("won", "lost", "timed_out", "died_to_shape"):
            add(k, obs[k].astype(np.float64), ended)
        met = ended & (obs["first_seen"] >= 0)
        add("met", np.ones(len(mine)), met)
        add("first_seen", obs["first_seen"].astype(np.float64), met)
        add("episodes", np.ones(len(mine)), ended)
        left[mine_duel[ended]] -= 1
    results = []
    for i in range(len(jobs)):
        totals = {k: int(round(sums[k][i])) if k in COUNTS else float(sums[k][i]) for k in TOTALS}
        n = totals["episodes"]
        totals["win"] = wilson(totals["won"], n)
        totals["loss"] = wilson(totals["lost"], n)
        totals["hit_rate"] = totals["hits"] / max(totals["shots"], 1)
        totals["dealt_per_episode"] = totals["dealt"] / max(n, 1)
        totals["taken_per_episode"] = totals["taken"] / max(n, 1)
        totals["decisions_per_episode"] = totals["decisions"] / max(n, 1)
        results.append(totals)
    return results


def evaluate_ffa(policy, config, episodes=200, games=100, threads=8, seed=12345, deterministic=True, opponent=None):
    """The policy in the first seat of free-for-all games and opponent, by default the policy too, in every other.
    The results are the first seat's: last one standing, dead, or alive when time ran out, and its kills."""
    per_game = math.ceil(episodes / games)
    opponent = policy if opponent is None else opponent
    config.kinds = [diepenv.EXTERNAL] * config.players
    if opponent is policy:
        config.remember = [config.remember[0]] * config.players
    vec = diepenv.Vec(config, games, threads)
    vec.seed(seed)
    vec.reset_all()
    slot = np.array([s for _, s in vec.row_map])
    game = np.array([d for d, _ in vec.row_map])
    mine, theirs = np.flatnonzero(slot == 0), np.flatnonzero(slot != 0)
    left = np.full(games, per_game)
    totals = {"won": 0, "lost": 0, "timed_out": 0, "episodes": 0, "kills": 0, "stolen": 0, "decisions": 0,
              "dealt": 0.0, "taken": 0.0}
    full = vec.observe()
    actions = Actions(vec.rows)
    while (left > 0).any():
        a, _ = act(policy, {k: full[k][mine] for k in OBS_KEYS}, deterministic)
        actions.set(mine, a)
        b, _ = act(opponent, {k: full[k][theirs] for k in OBS_KEYS}, deterministic)
        actions.set(theirs, b)
        full = actions.step(vec)
        live = left > 0
        for k in ("kills", "stolen"):
            totals[k] += int(full[k][mine][live].sum())
        for k in ("dealt", "taken"):
            totals[k] += float(full[k][mine][live].sum())
        totals["decisions"] += int(live.sum())
        for k in ("won", "lost", "timed_out"):
            totals[k] += int((full[k][mine] & live & full["done"][mine]).sum())
        over = np.bincount(game, full["game_over"], games) > 0
        totals["episodes"] += int((over & live).sum())
        left -= over
    n = totals["episodes"]
    totals["win"] = wilson(totals["won"], n)
    return totals


def describe_ffa(r):
    n = max(r["episodes"], 1)
    p, lo, hi = r["win"]
    return (f"last {p:.1%} [{lo:.1%}, {hi:.1%}]  died {r['lost'] / n:.1%}  survived {r['timed_out'] / n:.1%}"
            f"  kills {r['kills'] / n:.2f} (stolen {r['stolen'] / max(r['kills'], 1):.0%})"
            f"  dealt {r['dealt'] / n:.2f}  taken {r['taken'] / n:.2f}  len {r['decisions'] / n:.0f}  n={r['episodes']}")


def evaluate_team(policy, config, episodes=200, games=100, threads=8, seed=12345, deterministic=True, opponent=None):
    """The policy as the first team of team games and opponent, by default the policy too, as every other. The results
    are the first team's: won, lost or out of time, wins with the whole team still alive, and its kills."""
    per_game = math.ceil(episodes / games)
    opponent = policy if opponent is None else opponent
    config.kinds = [diepenv.EXTERNAL] * config.players
    teams = np.array(config.teams)
    size = int((teams == teams[0]).sum())
    vec = diepenv.Vec(config, games, threads)
    vec.seed(seed)
    vec.reset_all()
    slot = np.array([s for _, s in vec.row_map])
    game = np.array([d for d, _ in vec.row_map])
    first = teams[slot] == teams[0]
    mine, theirs, head = np.flatnonzero(first), np.flatnonzero(~first), np.flatnonzero(slot == 0)
    left = np.full(games, per_game)
    totals = {"won": 0, "lost": 0, "timed_out": 0, "episodes": 0, "whole": 0, "kills": 0, "decisions": 0,
              "dealt": 0.0, "taken": 0.0}
    full = vec.observe()
    actions = Actions(vec.rows)
    while (left > 0).any():
        a, _ = act(policy, {k: full[k][mine] for k in OBS_KEYS}, deterministic)
        actions.set(mine, a)
        b, _ = act(opponent, {k: full[k][theirs] for k in OBS_KEYS}, deterministic)
        actions.set(theirs, b)
        full = actions.step(vec)
        live = left > 0
        playing = live[game[mine]]
        totals["kills"] += int(full["kills"][mine][playing].sum())
        for k in ("dealt", "taken"):
            totals[k] += float(full[k][mine][playing].sum())
        totals["decisions"] += int(live.sum())
        over = full["game_over"][head] & live
        won, lost = full["team_won"][head] & over, full["team_lost"][head] & over
        totals["won"] += int(won.sum())
        totals["lost"] += int(lost.sum())
        totals["timed_out"] += int((over & ~won & ~lost).sum())
        totals["whole"] += int((won & (full["team_left"][head] == size)).sum())
        totals["episodes"] += int(over.sum())
        left -= full["game_over"][head]
    totals["win"] = wilson(totals["won"], totals["episodes"])
    return totals


def describe_team(r):
    n = max(r["episodes"], 1)
    p, lo, hi = r["win"]
    return (f"win {p:.1%} [{lo:.1%}, {hi:.1%}]  loss {r['lost'] / n:.1%}  draw {r['timed_out'] / n:.1%}"
            f"  whole team left {r['whole'] / max(r['won'], 1):.0%} of wins  kills {r['kills'] / n:.2f}"
            f"  dealt {r['dealt'] / n:.2f}  taken {r['taken'] / n:.2f}  len {r['decisions'] / n:.0f}  n={r['episodes']}")


def shape_results(r):
    """Damage from shapes per game, the share of shots a shape stopped, and deaths to shapes per game."""
    n = max(r["episodes"], 1)
    return {"shape_taken": r["shape_taken"] / n, "shape_blocked": r["shape_blocked"] / max(r["shots"], 1),
            "died_to_shape": r["died_to_shape"] / n}


def describe(r):
    p, lo, hi = r["win"]
    return (f"win {p:.1%} [{lo:.1%}, {hi:.1%}]  loss {r['loss'][0]:.1%}  draw {r['timed_out'] / max(r['episodes'], 1):.1%}"
            f"  hit {r['hit_rate']:.1%}  dealt {r['dealt_per_episode']:.2f}  taken {r['taken_per_episode']:.2f}"
            f"  len {r['decisions_per_episode']:.0f}  n={r['episodes']}"
            + (f"  met {r['met'] / max(r['episodes'], 1):.0%} after {r['first_seen'] / max(r['met'], 1):.0f}"
               if r["met"] < r["episodes"] else "")
            + ("  shapes: taken {shape_taken:.2f} blocked {shape_blocked:.0%} died {died_to_shape:.1%}".format(
                **shape_results(r)) if r["shape_taken"] or r["shape_blocked"] else ""))


def main():
    p = argparse.ArgumentParser()
    p.add_argument("checkpoint")
    p.add_argument("--opponent", default="fighter", help="a scripted opponent, or a checkpoint to play against")
    p.add_argument("--episodes", type=int, default=400)
    p.add_argument("--duels", type=int, default=200)
    p.add_argument("--threads", type=int, default=8)
    p.add_argument("--seed", type=int, default=12345)
    p.add_argument("--stochastic", action="store_true")
    p.add_argument("--aim-noise", type=float, default=0.0)
    p.add_argument("--cap", type=int, default=None, help="objects each tank is shown; 40 replays the first bots' evals")
    p.add_argument("--opp-tank", default=None,
                   help="the opponent's class; defaults to the checkpoint's own, or for scripted ones the trained against")
    p.add_argument("--tank", default=None, help="the class an any-class checkpoint plays")
    p.add_argument("--shapes", type=float, default=0.0, help="shapes, as a multiple of FFA's density")
    p.add_argument("--blind", default="", help="kinds the checkpoint isn't shown, so far only shapes")
    args = p.parse_args()

    policy, env_args = load(args.checkpoint)
    env_args["aim_noise"] = args.aim_noise
    tanks, stats = tanks_of(env_args), builds_of(env_args)
    opponent = None
    if args.opponent in KINDS:
        kind = args.opponent
    else:
        opponent, opp_env = load(args.opponent)
        tanks[1], stats[1] = tanks_of(opp_env)[0], builds_of(opp_env)[0]
        kind = "self"
    if args.tank:
        pick_classes(tanks, stats, 0, args.tank, [policy])
    if args.opp_tank:
        pick_classes(tanks, stats, 1, args.opp_tank, [opponent if opponent is not None else policy])
    env_args["tanks"], env_args["stats"] = tanks, stats
    if args.cap:
        env_args["entity_cap"] = args.cap
    env_args.update(shapes_min=args.shapes, shapes_max=args.shapes)
    config = make_config(kind, **env_args)
    r = evaluate(policy, config, args.episodes, args.duels, args.threads, args.seed, not args.stochastic, opponent,
                 tuple(filter(None, args.blind.split(","))))
    print(f"{args.opponent}: {describe(r)}")


if __name__ == "__main__":
    main()
