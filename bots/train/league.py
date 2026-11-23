"""Round robin between checkpoints, each playing the class it trained as.

    python league.py ../runs/a/it01000.pt ../runs/b/it01000.pt ... --episodes 400
"""
import argparse
import itertools
import json

import numpy as np

from common import make_config, tanks_of, builds_of, versus, TANK_NAMES
from evaluate import evaluate, load
from watch import name


def ratings(score, games):
    """Elo-scale Bradley-Terry fit; scores are clipped so a clean sweep stays finite."""
    n = len(score)
    s = np.clip(score, 0.5 / games, 1 - 0.5 / games)
    r = np.zeros(n)
    for _ in range(5000):
        e = 1 / (1 + 10 ** ((r[None, :] - r[:, None]) / 400))
        g = np.nansum(np.where(np.isnan(s), 0, s - e), 1)
        r += 10 * g
        r -= r.mean()
    return r


def main():
    p = argparse.ArgumentParser()
    p.add_argument("checkpoints", nargs="+")
    p.add_argument("--episodes", type=int, default=400)
    p.add_argument("--threads", type=int, default=8)
    p.add_argument("--seed", type=int, default=7)
    p.add_argument("--out", default=None, help="also write the results as json")
    p.add_argument("--cap", type=int, default=None, help="objects each tank is shown; 40 replays the first leagues")
    args = p.parse_args()

    bots = [load(c) for c in args.checkpoints]
    names = [f"{name(c)} ({TANK_NAMES[tanks_of(e)[0]]})" for c, (_, e) in zip(args.checkpoints, bots)]
    n = len(bots)
    # score[i, j]: i's wins against j plus half the draws.
    score = np.full((n, n), np.nan)
    games = []
    for i, j in itertools.combinations(range(n), 2):
        (pi, ei), (pj, ej) = bots[i], bots[j]
        env = versus(ei, tanks_of(ej)[0], builds_of(ej)[0])
        if args.cap:
            env["entity_cap"] = args.cap
        r = evaluate(pi, make_config("self", **env), args.episodes, threads=args.threads, seed=args.seed, opponent=pj)
        draw = r["timed_out"] / max(r["episodes"], 1)
        score[i, j] = r["win"][0] + draw / 2
        score[j, i] = r["loss"][0] + draw / 2
        games.append({"a": names[i], "b": names[j], "win": r["win"][0], "loss": r["loss"][0], "draw": draw,
                      "episodes": r["episodes"]})
        print(f"{names[i]} vs {names[j]}: win {r['win'][0]:.0%} loss {r['loss'][0]:.0%} draw {draw:.0%}", flush=True)

    elo = ratings(score, args.episodes)
    order = np.argsort(-elo)
    width = max(len(s) for s in names)
    print()
    print(" " * (width + 7) + "".join(f"{k:>5}" for k in range(1, n + 1)))
    for rank, i in enumerate(order, 1):
        cells = "".join("    -" if i == j else f"{score[i, j] * 100:5.0f}" for j in order)
        print(f"{rank:>2} {names[i]:<{width}} {elo[i]:+5.0f}{cells}")
    print("\nRows are each bot's score (wins plus half the draws, %) against the bots in the columns, ranked by rating.")
    if args.out:
        with open(args.out, "w") as f:
            json.dump({"names": names, "elo": elo.tolist(), "score": np.nan_to_num(score, nan=-1).tolist(),
                       "games": games}, f, indent=1)


if __name__ == "__main__":
    main()
