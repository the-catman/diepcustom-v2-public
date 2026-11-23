"""Side-by-side summary of training runs from their log.jsonl files.

    python compare.py ../runs/a ../runs/b ...
"""
import argparse
import json
import os

KINDS = ("still", "linear", "strafe", "fighter", "self")


def load(run):
    rows = []
    with open(os.path.join(run, "log.jsonl")) as f:
        for line in f:
            rows.append(json.loads(line))
    # A restarted run appends; keep the newest row for each iteration.
    by_iter = {r["iter"]: r for r in rows}
    return [by_iter[i] for i in sorted(by_iter)]


def recent(rows, kind, n):
    """Training results against one opponent over the last n iterations, weighted by episodes."""
    eps = won = lost = 0
    hit = 0.0
    for r in rows[-n:]:
        k = r.get(kind)
        if not k or not k["episodes"]:
            continue
        e = k["episodes"]
        eps += e
        won += k["win"] * e
        lost += k["loss"] * e
        hit += k["hit_rate"] * e
    if not eps:
        return None
    return won / eps, lost / eps, hit / eps, eps


def main():
    p = argparse.ArgumentParser()
    p.add_argument("runs", nargs="+")
    p.add_argument("--window", type=int, default=10, help="iterations to average training results over")
    args = p.parse_args()

    for run in args.runs:
        rows = load(run) if os.path.exists(os.path.join(run, "log.jsonl")) else []
        if not rows:
            print(f"== {os.path.basename(os.path.normpath(run))}  not started")
            continue
        last = rows[-1]
        print(f"== {os.path.basename(os.path.normpath(run))}  iter {last['iter']}  "
              f"{last['steps'] / 1e6:.1f}M decisions  ent {last['ent']:.2f}  lead std {last['lead_std']:.3f}")
        parts = []
        for kind in KINDS:
            r = recent(rows, kind, args.window)
            if r:
                parts.append(f"{kind} win {r[0]:.0%} loss {r[1]:.0%} hit {r[2]:.0%}")
        print("   train (last %d): " % args.window + "  |  ".join(parts))
        evals = [r for r in rows if "eval" in r]
        if evals:
            line = []
            for r in evals[-8:]:
                e = r["eval"]
                line.append(f"{r['iter']}: {e['win'][0]:.0%} [{e['win'][1]:.0%},{e['win'][2]:.0%}] hit {e['hit_rate']:.0%}")
            print(f"   eval vs {evals[-1]['eval']['opponent']}: " + "   ".join(line))
        evals = [r for r in rows if "eval_vs" in r]
        if evals:
            line = []
            for r in evals[-8:]:
                e = r["eval_vs"]
                line.append(f"{r['iter']}: {e['win'][0]:.0%}/{e['loss']:.0%}/{e['draw']:.0%}")
            print(f"   head-to-head win/loss/draw vs {evals[-1]['eval_vs']['opponent']}: " + "   ".join(line))


if __name__ == "__main__":
    main()
