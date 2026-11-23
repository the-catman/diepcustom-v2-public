import json
import math
import subprocess
import sys
from types import SimpleNamespace

import numpy as np
import torch

from common import diepenv, make_config, tank_id
from model import Policy, widen, BASE_SELF_DIM
from ppo import build_envs, spawn_range
from test_env import idle
from test_resume import SMALL, TRAIN

POS, SEEN_CAP = 1000, 64


def away_from_enemy(v):
    a, b = v.duel_state(0)["tanks"][:2]
    ang = math.atan2(a["y"] - b["y"], a["x"] - b["x"])
    return max(range(1, 9), key=lambda m: math.cos((m - 1) * math.pi / 4 - ang))


def test_the_tank_remembers_where_it_last_saw_the_enemy():
    c = make_config("still", arena_size=20000, spawn_min=500, spawn_max=500, max_decisions=1000)
    v = diepenv.Vec(c, 1, 1)
    v.seed(1)
    v.reset_all()
    o = v.observe()
    s = o["self"][0]
    assert s[40] == 0 and s[39] == 0
    a, b = v.duel_state(0)["tanks"][:2]
    assert np.allclose(s[34:36], [(b["x"] - a["x"]) / POS, (b["y"] - a["y"]) / POS], atol=1e-4)
    assert np.allclose(s[32:34], [a["x"] / 10000, a["y"] / 10000], atol=1e-4)

    move, fire, target, aim = idle(1)
    move[:] = away_from_enemy(v)
    gone = None
    for step in range(1, 200):
        o = v.step(move, fire, target, aim)
        if gone is None and o["self"][0][39] > 0:
            gone = step
        if gone is not None and step - gone >= 10:
            break
    assert gone is not None, "never lost sight of the enemy"
    s = o["self"][0]
    a, b = v.duel_state(0)["tanks"][:2]
    # The sighting stays put while I move away: the offset is recomputed from where I am now.
    assert np.allclose(s[34:36], [(b["x"] - a["x"]) / POS, (b["y"] - a["y"]) / POS], atol=1e-3)
    assert s[39] == np.float32(11 / SEEN_CAP)
    assert s[40] == 0


def test_a_tank_that_never_saw_the_enemy_knows_it():
    c = make_config("still", arena_size=20000, spawn_min=6000, spawn_max=6000, max_decisions=3)
    v = diepenv.Vec(c, 1, 1)
    v.seed(2)
    v.reset_all()
    s = v.observe()["self"][0]
    assert s[40] == 1 and s[39] == 1 and not s[34:39].any()
    for _ in range(3):
        o = v.step(*idle(1))
    assert o["done"][0] and o["first_seen"][0] == -1
    near = diepenv.Vec(make_config("still", spawn_min=500, spawn_max=500, max_decisions=3), 1, 1)
    near.reset_all()
    near.observe()
    for _ in range(3):
        o = near.step(*idle(1))
    assert o["done"][0] and o["first_seen"][0] == 0


def test_widened_old_weights_play_the_same():
    torch.manual_seed(0)
    old = Policy(d=32, layers=1)
    new = Policy(d=32, layers=1, self_dim=diepenv.SELF_DIM)
    new.load_state_dict(widen(old.state_dict(), diepenv.SELF_DIM))
    v = diepenv.Vec(make_config("fighter"), 4, 1)
    v.reset_all()
    obs = {k: torch.from_numpy(x) for k, x in v.observe().items() if k in ("self", "ents", "mask", "targetable")}
    assert old.self_dim == BASE_SELF_DIM
    a, _ = old(obs)
    b, _ = new(obs)
    for k in a:
        assert torch.allclose(a[k], b[k]), k


def test_far_spawns_ramp_and_anchors_stay_close():
    args = SimpleNamespace(mix="strafe:1,self:1", duels=2, threads=1, close_anchors=True, spawn_ramp=100,
                           spawn_start="1500:3000", spawn_min=2000, spawn_max=5000)
    assert spawn_range(args, 1) == (1500, 3000)
    assert spawn_range(args, 51) == (1750, 4000)
    assert spawn_range(args, 500) == (2000, 5000)
    env_args = dict(arena_size=6000, spawn_min=2000, spawn_max=5000, mirror_spawn=True)
    vec, _, row_kind, *_, far, _ = build_envs(args, env_args, 1)
    assert [d for d, _ in far] == [1] and far[0][1].mirror_spawn
    near, distant = (vec.duel_state(d)["tanks"][:2] for d in (0, 1))
    assert math.hypot(near[0]["x"] - near[1]["x"], near[0]["y"] - near[1]["y"]) <= 1200
    assert math.hypot(distant[0]["x"] - distant[1]["x"], distant[0]["y"] - distant[1]["y"]) >= 2000


def test_a_run_with_last_seen_starts_from_an_older_bot(tmp_path):
    old = tmp_path / "old"
    r = subprocess.run([sys.executable, "ppo.py", "--out", str(old), *SMALL, "--iters", "1"],
                       cwd=TRAIN, capture_output=True, text=True)
    assert r.returncode == 0, r.stderr
    new = tmp_path / "new"
    r = subprocess.run([sys.executable, "ppo.py", "--out", str(new), *SMALL, "--iters", "2", "--last-seen",
                        "--init", str(old / "it00001.pt"), "--arena", "6000", "--spawn-min", "2000",
                        "--spawn-max", "5000", "--mirror-spawn", "--close-anchors", "--spawn-ramp", "2"],
                       cwd=TRAIN, capture_output=True, text=True)
    assert r.returncode == 0, r.stderr
    rows = [json.loads(line) for line in open(new / "log.jsonl")]
    assert rows[0]["spawn"] == [1500, 3000] and rows[-1]["spawn"] == [1750, 4000]
    assert {"met", "first_seen"} <= set(rows[-1]["self"])
    ckpt = torch.load(new / "latest.pt", weights_only=False)
    assert ckpt["model_args"]["self_dim"] == diepenv.OLD_SELF_DIM


def test_both_screens_lie_the_same_way():
    c = make_config("self", arena_size=20000, spawn_min=2000, spawn_max=2000, symmetry=True)
    v = diepenv.Vec(c, 300, 4)
    v.seed(4)
    v.reset_all()
    seen = v.observe()["targetable"].any(1).reshape(-1, 2)
    # 2000 apart, a tank is on screen at some angles and off it at others.
    assert 0 < seen[:, 0].sum() < len(seen)
    assert (seen[:, 0] == seen[:, 1]).all()


def test_every_screen_is_as_wide_as_a_players():
    c = make_config("self", arena_size=20000, spawn_min=1000, spawn_max=2200, symmetry=True)
    v = diepenv.Vec(c, 400, 4)
    v.seed(9)
    v.reset_all()
    seen = v.observe()["targetable"].any(1)
    wide = []
    for d, s in v.row_map:
        tanks = v.duel_state(d)["tanks"]
        me, other = tanks[s], tanks[1 - s]
        r = other["size"]
        wide.append(abs(other["x"] - me["x"]) <= 960 / me["fov"] + r and abs(other["y"] - me["y"]) <= 540 / me["fov"] + r)
    assert 0 < seen.sum() < len(seen)
    assert (seen == np.array(wide)).all()


def test_a_step_cost_comes_off_every_decision(tmp_path):
    means = []
    for cost in ("0", "0.01"):
        out = tmp_path / cost
        r = subprocess.run([sys.executable, "ppo.py", "--out", str(out), *SMALL, "--iters", "1", "--step-cost", cost],
                           cwd=TRAIN, capture_output=True, text=True)
        assert r.returncode == 0, r.stderr
        means.append(json.loads(open(out / "log.jsonl").readline())["mean_reward"])
    assert abs(means[0] - means[1] - 0.01) < 1e-5
