import hashlib
import os
import subprocess
import sys

import numpy as np

from common import diepenv, make_config, tank_id

# What the first 41 self features and 28 entity features were before any were added after them.
OLD_SELF, OLD_ENT = 41, 28
PINNED = "73b85e84f47712a2334e7571c2d8766a23a69fa76f8f7b875cb2415f04aa731d"


def trajectory():
    """A seeded game of random actions between bullet and drone classes, hashed: rewards, credits, endings
    and the observation features the older bots see."""
    pairs = [("fighter", "penta-shot"), ("overseer", "tank"), ("spread-shot", "factory"), ("sniper", "twin")]
    configs = []
    for a, b in pairs:
        c = make_config("self", arena_size=3000, max_decisions=60, symmetry=True)
        c.tanks = [tank_id(a), tank_id(b)]
        configs.append(c)
    configs.append(make_config("fighter", arena_size=3000, max_decisions=60))
    v = diepenv.Vec(configs, 1)
    v.seed(5)
    v.reset_all()
    rng = np.random.default_rng(3)
    h = hashlib.sha256()
    o = v.observe()
    n = v.rows
    for _ in range(150):
        h.update(np.ascontiguousarray(o["self"][:, :OLD_SELF]).tobytes())
        h.update(np.ascontiguousarray(o["ents"][:, :, :OLD_ENT]).tobytes())
        h.update(o["mask"].tobytes())
        h.update(o["targetable"].tobytes())
        targets = np.array([rng.choice(np.flatnonzero(t)) if t.any() and rng.random() < 0.7 else -1
                            for t in o["targetable"]], np.int32)
        reach = np.where(rng.random(n) < 0.5, rng.normal(0, 0.5, n), np.nan).astype(np.float32)
        o = v.step(rng.integers(0, 9, n).astype(np.int32), rng.random(n) < 0.6, targets,
                   rng.normal(0, 0.3, n).astype(np.float32), rng.random(n) < 0.2, reach)
        for k in ("reward", "done", "won", "lost", "timed_out", "dealt", "taken", "shots", "hits", "first_seen",
                  "credit_row", "credit_ago", "credit_value"):
            h.update(np.ascontiguousarray(o[k]).tobytes())
    return h.hexdigest()


def test_old_features_and_rewards_are_unchanged():
    # Drone AIs take turns from a counter shared by every game in the process, so the game is only
    # repeatable in a fresh one, on one thread.
    here = os.path.dirname(os.path.abspath(__file__))
    code = "import conftest, test_pin; print(test_pin.trajectory())"
    got = subprocess.run([sys.executable, "-c", code], cwd=here, capture_output=True, text=True, check=True)
    assert got.stdout.split()[-1] == PINNED
