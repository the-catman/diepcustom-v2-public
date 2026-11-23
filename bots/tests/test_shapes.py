import argparse
import json
import math
import subprocess
import sys

import numpy as np
import torch

from common import diepenv, make_config, tank_id
from model import Policy, adapt
from ppo import shape_args
from test_resume import SMALL, TRAIN

K = diepenv.CLASS_ENT_DIM
FFA_4000 = round(2000 / 22300 ** 2 * 4000 ** 2)


def idle(n):
    return np.zeros(n, np.int32), np.zeros(n, bool), np.full(n, -1, np.int32), np.zeros(n, np.float32)


def duels(n=4, opponent="self", **kw):
    c = make_config(opponent, **dict(dict(arena_size=4000, spawn_min=1000, spawn_max=3000), **kw))
    c.tanks = [tank_id("fighter")] * 2
    v = diepenv.Vec(c, n, 1)
    v.seed(2)
    v.reset_all()
    return v


def kinds(v, d):
    e = v.duel_entities(d)
    return e[e[:, 3] >= 3], e[e[:, 3] == 0]


def test_no_shapes_unless_asked():
    v = duels()
    for _ in range(5):
        v.step(*idle(v.rows))
    assert [v.duel_state(d)["shapes"] for d in range(v.duels)] == [0] * v.duels


def test_shapes_fill_the_arena_before_the_first_decision():
    v = duels(shapes_min=1, shapes_max=1)
    for d in range(v.duels):
        assert v.duel_state(d)["shapes"] == FFA_4000
        shapes, tanks = kinds(v, d)
        assert len(shapes) == FFA_4000
        # Everywhere, not in a ring away from the tanks, yet clear of them.
        assert {(x > 0, y > 0) for x, y in shapes[:, :2]} == {(a, b) for a in (0, 1) for b in (0, 1)}
        for t in tanks:
            gap = np.hypot(shapes[:, 0] - t[0], shapes[:, 1] - t[1]) - shapes[:, 2] - t[2]
            assert gap.min() >= 149


def test_each_game_draws_its_density():
    v = duels(40, shapes_min=0.5, shapes_max=1.25, shapes_empty=0.3)
    counts = []
    for _ in range(3):
        counts += [v.duel_state(d)["shapes"] for d in range(v.duels)]
        v.reset_all()
    counts = np.array(counts)
    full = counts[counts > 0]
    assert 0.15 < (counts == 0).mean() < 0.45
    assert full.min() >= round(0.5 * FFA_4000) - 1 and full.max() <= round(1.25 * FFA_4000) + 1
    assert full.std() > 5


def test_field_mix_and_pentagon_rich_games():
    def shares(**kw):
        v = duels(8, shapes_min=4, shapes_max=4, **kw)
        per = []
        for d in range(v.duels):
            sides = kinds(v, d)[0][:, 3]
            per.append([(sides == k).mean() for k in (3, 4, 5)])
        return np.array(per)

    field = shares().mean(0)
    assert abs(field[0] - 0.16) < 0.03 and abs(field[2] - 0.04) < 0.02
    rich = shares(pentagon_rich=1)
    assert (rich[:, 2] > 0.15).all() and (rich[:, 2] < 0.45).all()


def test_shapes_are_flagged_and_listed_after_everything_else():
    v = duels(shapes_min=1.25, shapes_max=1.25)
    for _ in range(10):
        o = v.step(np.full(v.rows, 1, np.int32), np.ones(v.rows, bool), np.full(v.rows, -1, np.int32),
                   np.zeros(v.rows, np.float32))
    for r in range(v.rows):
        rows = o["ents"][r][o["mask"][r]]
        shape = rows[:, K] == 1
        assert shape.sum() > 10 and (~shape).sum() > 0
        # Every non-shape first, each part nearest first.
        assert not (np.diff(shape.astype(int)) < 0).any()
        for part in (rows[shape], rows[~shape]):
            assert (np.diff(part[:, 2]) >= -1e-6).all()
        # Squares, triangles, pentagons and alphas, and the rare shiny ones with ten times the health.
        assert {round(float(x), 2) for x in rows[shape, K + 1]} <= {0.29, 0.42, 0.57, 0.71, 0.86, 0.99}
        assert not rows[shape, 16:18].any() and not rows[shape, K + 2].any()
        assert not rows[~shape, K:].any()
        assert not o["targetable"][r][o["mask"][r]][shape].any()


def test_bumping_a_shape_counts_as_damage_taken():
    v = duels(1, opponent="still")
    me = v.duel_state(0)["tanks"][0]
    v.duel_place_shape(0, 5, me["x"] + me["size"], me["y"])
    taken, shape_taken, reward = 0.0, 0.0, 0.0
    for _ in range(10):
        o = v.step(*idle(1))
        taken += o["taken"][0]
        shape_taken += o["shape_taken"][0]
        reward += o["reward"][0]
    assert shape_taken > 0.05
    assert math.isclose(taken, shape_taken, rel_tol=1e-5)
    assert math.isclose(reward, -0.5 * shape_taken, rel_tol=1e-4)


def test_dying_to_a_shape_is_a_loss():
    v = duels(1)
    me = v.duel_state(0)["tanks"][0]
    for a in range(6):
        v.duel_place_shape(0, 6, me["x"] + 60 * math.cos(a), me["y"] + 60 * math.sin(a))
    for _ in range(40):
        o = v.step(*idle(v.rows))
        if o["done"][0]:
            break
    assert o["done"][0] and o["died_to_shape"][0] and not o["died_to_shape"][1]
    assert o["lost"][0] and o["won"][1]


def test_shots_into_a_shape_count_as_blocked():
    v = duels(1, opponent="still")
    me = v.duel_state(0)["tanks"][0]
    v.duel_place_shape(0, 6, me["x"] + 300, me["y"])
    blocked = shots = 0
    for _ in range(30):
        move, fire, target, aim = idle(1)
        o = v.step(move, np.ones(1, bool), target, aim)
        blocked += o["shape_blocked"][0]
        shots += o["shots"][0]
    assert blocked > 0 and blocked <= shots


def test_a_policy_from_before_shapes_plays_the_same_with_the_new_inputs():
    torch.manual_seed(0)
    kw = dict(d=32, heads=2, layers=1, self_dim=diepenv.SELF_DIM, classes=8, any_class=True)
    old = Policy(ent_dim=K, **kw)
    new = Policy(ent_dim=diepenv.ENT_DIM, **kw)
    new.load_state_dict(adapt(old.state_dict(), new))
    o = duels(2, shapes_min=1, shapes_max=1).observe()
    obs = {k: torch.from_numpy(o[k]) for k in ("self", "ents", "mask", "targetable", "tank")}
    obs["tank"] = obs["tank"].clamp(0, 7)
    a, _ = old(obs)
    b, _ = new(obs)
    for k in ("move", "fire", "target", "value"):
        assert torch.allclose(a[k], b[k], atol=1e-5)


def test_shapes_grow_over_the_ramp():
    args = argparse.Namespace(shapes="0:1.25", shape_ramp=100, shape_empty=0.2, pentagon_rich=0.15)
    assert shape_args(args, 1)["shapes_max"] == 0
    assert math.isclose(shape_args(args, 51)["shapes_max"], 0.625)
    assert shape_args(args, 101)["shapes_max"] == shape_args(args, 500)["shapes_max"] == 1.25
    assert shape_args(args, 500)["shapes_empty"] == 0.2
    assert shape_args(argparse.Namespace(shapes=None), 1) == {}


def test_a_run_with_shapes_logs_what_they_did(tmp_path):
    bot = tmp_path / "bot"
    r = subprocess.run([sys.executable, "ppo.py", "--out", str(bot), *SMALL, "--iters", "1", "--tank", "fighter",
                        "--last-seen"], cwd=TRAIN, capture_output=True, text=True)
    assert r.returncode == 0, r.stderr
    ckpt = str(bot / "it00001.pt")
    out = tmp_path / "run"
    r = subprocess.run([sys.executable, "ppo.py", "--out", str(out), *SMALL, "--iters", "3", "--arena", "4000",
                        "--classes", "fighter:2,penta-shot", "--mix", "self:1,live:2,frozen:1", "--frozen", ckpt,
                        "--shapes", "0:1.25", "--shape-ramp", "2", "--shape-empty", "0.2", "--pentagon-rich", "0.15",
                        "--shape-vs", ckpt, "--init", ckpt, "--frozen-eval-every", "3", "--frozen-eval-episodes", "4",
                        "--max-decisions", "12"], cwd=TRAIN, capture_output=True, text=True)
    assert r.returncode == 0, r.stderr
    rows = [json.loads(line) for line in open(out / "log.jsonl")]
    assert [row["shapes"][1] for row in rows] == [0, 0.625, 1.25]
    assert {"shape_taken", "shape_blocked", "died_to_shape"} <= set(rows[-1]["live"])
    assert set(rows[-1]["field_shapes_eval"]) == {"Fighter", "Penta Shot"}
    assert set(rows[-1]["shape_eval"]["bot/it00001"]) == {"shapes", "none"}
    none = rows[-1]["shape_eval"]["bot/it00001"]["none"]
    assert none["shape_taken"] == none["shape_blocked"] == 0


def test_shapes_are_on_nobodys_side():
    v = duels(4, shapes_min=1, shapes_max=1, spawn_min=600, spawn_max=1000)
    o = v.step(*idle(v.rows))
    for r in range(v.rows):
        rows = o["ents"][r][o["mask"][r]]
        shape = rows[:, K] == 1
        assert shape.sum() > 10
        # Neither an enemy nor drone-like, whoever's side the shape is on in the game.
        assert not rows[shape][:, [20, 22]].any()
        assert rows[~shape & (rows[:, 18] == 1), 22].sum() == 1


def test_a_tank_blind_to_shapes_is_shown_the_rest_moved_up_and_aims_where_it_meant():
    from evaluate import hide
    ents = np.zeros((1, 5, diepenv.ENT_DIM), np.float32)
    ents[0, :, 0] = [1, 2, 3, 4, 0]
    ents[0, [1, 2], K] = 1
    obs = {"ents": ents, "mask": np.array([[1, 1, 1, 1, 0]], bool), "targetable": np.array([[1, 0, 0, 1, 0]], bool)}
    shown, order = hide(obs, ("shapes",))
    assert list(shown["mask"][0]) == [1, 1, 0, 0, 0]
    assert list(shown["ents"][0, :2, 0]) == [1, 4]
    assert list(shown["targetable"][0]) == [1, 1, 0, 0, 0]
    # The second row shown was the fourth, so a target there goes to the fourth.
    assert order[0, 1] == 3
    shown, _ = hide(obs, ())
    assert (shown["ents"] == ents).all() and (shown["mask"] == obs["mask"]).all()


def test_blind_exams_change_nothing_without_shapes_and_play_with_them():
    from evaluate import evaluate, DEVICE
    torch.manual_seed(0)
    policy = Policy(d=32, heads=2, layers=1, self_dim=diepenv.SELF_DIM, ent_dim=diepenv.ENT_DIM, classes=40,
                    any_class=True).to(DEVICE)

    def run(blind, **kw):
        c = make_config("self", arena_size=4000, spawn_min=1000, spawn_max=3000, max_decisions=30, **kw)
        c.tanks = [tank_id("fighter")] * 2
        return evaluate(policy, c, 16, duels=8, threads=2, seed=4, opponent=policy, blind=blind)

    seen, blind = run(()), run(("shapes",))
    for k in ("won", "lost", "timed_out", "shots", "hits", "dealt"):
        assert seen[k] == blind[k]
    shaped = run(("shapes",), shapes_min=1, shapes_max=1)
    assert shaped["episodes"] == 16


def test_a_run_with_sentinels_runs_the_field_exam(tmp_path):
    out = tmp_path / "run"
    cmd = [sys.executable, "ppo.py", "--out", str(out), *SMALL, "--arena", "4000", "--spawn-min", "1000",
           "--spawn-max", "3000", "--classes", "fighter:2,penta-shot,ranger", "--mix", "self:1,live:2",
           "--sentinels", "penta-shot", "--frozen-eval-every", "2", "--frozen-eval-episodes", "4",
           "--max-decisions", "12", "--eval-episodes", "0", "--save-every", "1", "--free-lead", "--lr-iters", "20"]
    r = subprocess.run(cmd + ["--iters", "3"], cwd=TRAIN, capture_output=True, text=True)
    assert r.returncode == 0, r.stderr
    r = subprocess.run([sys.executable, "ppo.py", "--out", str(out), "--resume", "--iters", "4"], cwd=TRAIN,
                       capture_output=True, text=True)
    assert r.returncode == 0, r.stderr
    rows = [json.loads(line) for line in open(out / "log.jsonl")]
    assert len(rows) == 4
    # No scripted eval, but the checkpoints still come.
    assert "eval" not in rows[-1] and (out / "it00004.pt").exists()
    three = {"plain", "shapes", "shapes-blind"}
    assert {k: set(v) for k, v in rows[1]["field_exam"].items()} == {"Penta Shot": three}
    assert {k: set(v) for k, v in rows[-1]["field_exam"].items()} == {
        "Penta Shot": three, "Fighter": {"plain", "shapes"}, "Ranger": {"plain", "shapes"}}
    assert "field_eval" not in rows[-1]
    # The lead angle's share of the KL is logged on its own.
    assert all(row["kl_lead"] >= 0 for row in rows)
