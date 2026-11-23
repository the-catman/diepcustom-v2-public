import math
import pytest
import numpy as np

from common import diepenv, make_config, tank_id, ENT_MAX, SELF_DIM, ENT_DIM


def idle(n):
    return np.zeros(n, np.int32), np.zeros(n, bool), np.full(n, -1, np.int32), np.zeros(n, np.float32)


def target_row(obs, row=0):
    rows = np.flatnonzero(obs["targetable"][row])
    assert len(rows) == 1
    return int(rows[0])


def test_observation_shapes():
    v = diepenv.Vec(make_config("fighter"), 3, 2)
    o = v.observe()
    assert o["self"].shape == (3, SELF_DIM)
    # Entity arrays are as wide as the busiest row needs.
    assert o["ents"].shape[0] == 3 and o["ents"].shape[2] == ENT_DIM
    assert o["ents"].shape[1] == o["mask"].sum(1).max() <= ENT_MAX
    assert (o["mask"].sum(1) == o["seen"]).all()
    assert o["mask"].dtype == bool and o["targetable"].dtype == bool
    # The opponent spawns 600 to 1200 away, always on screen.
    assert o["targetable"].sum(1).tolist() == [1, 1, 1]


def shoot_once(credit):
    """Fires a single bullet at a still target on decision 0, then idles."""
    v = diepenv.Vec(make_config("still", spawn_min=900, spawn_max=900, credit_shots=credit), 1, 1)
    v.seed(3)
    v.reset_all()
    o = v.observe()
    t = target_row(o)
    move, fire, target, aim = idle(1)
    rewards, credits = [], []
    for step in range(12):
        f = fire.copy()
        f[0] = step == 0
        tg = np.array([t], np.int32)
        o = v.step(move, f, tg, aim)
        rewards.append(float(o["reward"][0]))
        credits += [(step, int(a), float(x)) for a, x in zip(o["credit_ago"], o["credit_value"])]
        if step == 0:
            assert o["shots"][0] == 1
    return rewards, credits


def test_hit_is_paid_on_the_firing_decision():
    rewards, credits = shoot_once(credit=True)
    assert credits, "the bullet never hit"
    for step, ago, value in credits:
        assert step - ago == 0, "credit should point back at decision 0"
        assert value > 0
    # Nothing paid on impact, apart from a bullet landing within its own decision.
    assert all(r == 0 for r in rewards[1:])
    impact = credits[0][0]
    assert impact >= 2, "900 units takes the bullet more than one decision"


def test_without_credit_the_hit_is_paid_on_impact():
    rewards, credits = shoot_once(credit=False)
    assert not credits
    paid = [i for i, r in enumerate(rewards) if r > 0]
    assert paid and paid[0] >= 2
    on, off = shoot_once(credit=True)
    assert abs(sum(rewards) - sum(x for _, _, x in off)) < 1e-4


def test_history_shows_a_reversal():
    v = diepenv.Vec(make_config("still"), 1, 1)
    move, fire, target, aim = idle(1)
    right, left = np.array([1], np.int32), np.array([5], np.int32)
    for _ in range(6):
        o = v.step(right, fire, target, aim)
    assert o["self"][0, 2] > 0 and o["self"][0, 6] > 0
    for _ in range(4):
        o = v.step(left, fire, target, aim)
    vel_now, avg4, avg16 = o["self"][0, 0], o["self"][0, 2], o["self"][0, 6]
    assert vel_now < 0 and avg4 < 0
    assert avg16 > avg4, "the 16 tick window still remembers moving right"


def test_same_seed_same_episode():
    def run():
        v = diepenv.Vec(make_config("fighter"), 4, 2)
        v.seed(11)
        v.reset_all()
        rng = np.random.default_rng(0)
        out = []
        for _ in range(40):
            o = v.step(rng.integers(0, 9, 4).astype(np.int32), rng.random(4) < 0.5,
                       np.full(4, -1, np.int32), rng.random(4).astype(np.float32) * 6)
            out.append(o["self"].copy())
        return np.stack(out)
    assert np.array_equal(run(), run())


def test_losing_pays_minus_one():
    v = diepenv.Vec(make_config("fighter"), 8, 4)
    move, fire, target, aim = idle(8)
    lost = 0
    for _ in range(400):
        o = v.step(move, fire, target, aim)
        for r in np.flatnonzero(o["done"]):
            assert o["lost"][r]
            assert o["reward"][r] <= -1 + 1e-6
            lost += 1
    assert lost >= 8


def test_self_play_rewards_mirror():
    c = make_config("self", credit_shots=False)
    v = diepenv.Vec(c, 16, 4)
    assert v.rows == 32
    rng = np.random.default_rng(1)
    total = np.zeros(32)
    ended = 0
    for _ in range(600):
        o = v.step(rng.integers(0, 9, 32).astype(np.int32), np.ones(32, bool), np.full(32, -1, np.int32),
                   rng.random(32).astype(np.float32) * 6.3)
        total += o["reward"]
        ended += int(o["done"].sum())
        a, b = o["won"].reshape(-1, 2), o["lost"].reshape(-1, 2)
        assert np.array_equal(a[:, 0], b[:, 1]) and np.array_equal(a[:, 1], b[:, 0])
    assert ended > 0
    # Each duel's rewards are zero-sum when dealt and taken use the same scale.
    assert np.allclose(total.reshape(-1, 2).sum(1), 0, atol=1e-3)


def test_targeted_aim_kills_a_still_tank():
    v = diepenv.Vec(make_config("still"), 16, 4)
    o = v.observe()
    move, fire, target, aim = idle(16)
    wins = 0
    for _ in range(60):
        tg = np.array([target_row(o, r) if o["targetable"][r].any() else -1 for r in range(16)], np.int32)
        o = v.step(move, np.ones(16, bool), tg, aim)
        wins += int(o["won"].sum())
    assert wins >= 16


def turn(x, y, g):
    if g >= 4:
        y = -y
    for _ in range(g & 3):
        x, y = -y, x
    return x, y


def turn_angle(sin, cos, g):
    a = np.arctan2(sin, cos)
    a = (-a if g >= 4 else a) + (g & 3) * np.pi / 2
    return np.sin(a), np.cos(a)


def turn_move(m, g):
    if m == 0:
        return 0
    a = (m - 1) * np.pi / 4
    a = (-a if g >= 4 else a) + (g & 3) * np.pi / 2
    return int(np.round(a / (np.pi / 4))) % 8 + 1


def expected_self(s, g, move, lead):
    e = s.copy()
    for i in (0, 2, 4, 6, 32, 34, 36):
        e[i], e[i + 1] = turn(s[i], s[i + 1], g)
    left, right, top, bottom = s[10:14]
    if g >= 4:
        top, bottom = bottom, top
    for _ in range(g & 3):
        left, right, top, bottom = bottom, top, left, right
    e[10:14] = left, right, top, bottom
    e[15:24] = 0
    e[15 + move] = 1
    e[26], e[27] = np.sin(lead), np.cos(lead)
    e[28], e[29] = turn_angle(s[28], s[29], g)
    return e


def expected_row(f, g):
    e = f.copy()
    for i in (0, 3, 5, 7, 9, 11):
        e[i], e[i + 1] = turn(f[i], f[i + 1], g)
    e[16], e[17] = turn_angle(f[16], f[17], g)
    return e


def test_symmetry_turns_the_view_and_the_controls_together():
    """The same fight seen in each of the 8 frames is the same fight, turned."""
    config = make_config("fighter", spawn_min=400, spawn_max=400)

    def fresh(seed, g):
        v = diepenv.Vec(config, 1, 1)
        v.seed(seed)
        v.reset_all()
        v.set_frame(0, 0, g)
        return v

    for g in range(1, 8):
        # Random numbers come from one stream per thread, so the plain fight is played out
        # first and the turned one replays it from the same seed.
        plain = fresh(5 + g, 0)
        rng = np.random.default_rng(g)
        o = plain.observe()
        seen, actions = [o], []
        for step in range(30):
            move, fire = int(rng.integers(0, 9)), bool(rng.random() < 0.5)
            aimed, angle = bool(rng.random() < 0.5), float(rng.uniform(-3, 3))
            actions.append((move, fire, aimed, angle * 0.2 if aimed else angle))
            o = plain.step(np.array([move], np.int32), np.array([fire]),
                           np.array([target_row(o) if aimed else -1], np.int32),
                           np.array([actions[-1][3]], np.float32))
            seen.append(o)
            if o["done"][0]:
                break

        turned = fresh(5 + g, g)
        ob = turned.observe()
        for step, (move, fire, aimed, aim) in enumerate(actions):
            oa = seen[step]
            ta, tb = target_row(oa), target_row(ob)
            np.testing.assert_allclose(ob["ents"][0, tb], expected_row(oa["ents"][0, ta], g), atol=1e-4)
            near_a = np.flatnonzero(oa["mask"][0] & (oa["ents"][0, :, 2] < 0.5))
            near_b = np.flatnonzero(ob["mask"][0] & (ob["ents"][0, :, 2] < 0.5))
            assert len(near_a) == len(near_b)
            for i in near_a:
                j = near_b[np.argmin(np.abs(ob["ents"][0, near_b, 2] - oa["ents"][0, i, 2]))]
                np.testing.assert_allclose(ob["ents"][0, j], expected_row(oa["ents"][0, i], g), atol=1e-4)

            if aimed:
                aim_b = -aim if g >= 4 else aim
            else:
                s, c = turn_angle(np.sin(aim), np.cos(aim), g)
                aim_b = float(np.arctan2(s, c))
            ob = turned.step(np.array([turn_move(move, g)], np.int32), np.array([fire]),
                             np.array([tb if aimed else -1], np.int32), np.array([aim_b], np.float32))
            oa = seen[step + 1]
            assert ob["reward"][0] == pytest.approx(oa["reward"][0], abs=1e-4)
            assert ob["done"][0] == oa["done"][0]
            if oa["done"][0]:
                break
            np.testing.assert_allclose(ob["self"][0], expected_self(oa["self"][0], g, turn_move(move, g),
                                                                    np.float32(aim_b)), atol=1e-4)


def test_symmetry_picks_frames_at_random():
    seen = set()
    for seed in range(32):
        plain = diepenv.Vec(make_config("fighter"), 1, 1)
        turned = diepenv.Vec(make_config("fighter", symmetry=True), 1, 1)
        for v in (plain, turned):
            v.seed(seed)
            v.reset_all()
        op, ot = plain.observe(), turned.observe()
        row_p, row_t = op["ents"][0, target_row(op)], ot["ents"][0, target_row(ot)]
        frames = [g for g in range(8)
                  if np.allclose(ot["self"][0], expected_self(op["self"][0], g, 0, 0), atol=1e-5)
                  and np.allclose(row_t, expected_row(row_p, g), atol=1e-5)]
        assert len(frames) == 1
        seen.add(frames[0])
    assert seen == {0, 2, 4, 6}


def test_each_side_plays_its_own_class():
    v = diepenv.Vec(make_config("still", tanks=[tank_id("fighter"), tank_id("sniper")]), 1, 1)
    assert len(v.duel_barrels(0, 0)) == 5
    assert len(v.duel_barrels(0, 1)) == 1
    fov = [t["fov"] for t in v.duel_state(0)["tanks"]]
    # A sniper sees further.
    assert fov[1] < fov[0]


def test_mirrored_spawns_start_on_opposite_sides():
    c = make_config("still", arena_size=6000, spawn_min=2000, spawn_max=5000, mirror_spawn=True)
    v = diepenv.Vec(c, 200, 4)
    v.seed(5)
    v.reset_all()
    for d in range(200):
        a, b = v.duel_state(d)["tanks"]
        assert abs(a["x"] + b["x"]) < 1e-6 and abs(a["y"] + b["y"]) < 1e-6
        assert 2000 <= math.hypot(a["x"] - b["x"], a["y"] - b["y"]) <= 5000
        assert max(abs(a["x"]), abs(a["y"])) < 2700


def test_a_paused_duel_sits_still_until_unpaused():
    v = diepenv.Vec(make_config("strafe", spawn_min=400, spawn_max=400), 2, 2)
    v.seed(1)
    v.reset_all()
    n = v.rows

    def step():
        return v.step(np.full(n, 3, np.int32), np.ones(n, bool), np.full(n, -1, np.int32), np.zeros(n, np.float32))

    def at(d):
        t = v.duel_state(d)["tanks"][0]
        return t["x"], t["y"]

    where = [at(0), at(1)]
    v.set_paused(np.array([True, False]))
    for _ in range(5):
        o = step()
        assert not o["done"][0] and (o["self"][0] == 0).all() and not o["mask"][0].any()
    assert at(0) == where[0] and at(1) != where[1]
    v.set_paused(np.array([False, False]))
    step()
    assert at(0) != where[0]


def test_jobs_played_together_each_get_their_own_games():
    import torch
    from evaluate import evaluate_many, DEVICE
    from model import Policy
    torch.manual_seed(0)
    policy = Policy(d=32, heads=2, layers=1, self_dim=diepenv.SELF_DIM, ent_dim=diepenv.ENT_DIM).to(DEVICE)
    other = Policy(d=32, heads=2, layers=1, self_dim=diepenv.SELF_DIM, ent_dim=diepenv.ENT_DIM).to(DEVICE)
    jobs = [{"config": make_config("still", max_decisions=20), "episodes": 12, "duels": 4},
            {"config": make_config("self", max_decisions=60), "episodes": 9, "duels": 3, "opponent": policy},
            {"config": make_config("self", max_decisions=40), "episodes": 6, "duels": 6, "opponent": other}]
    for cap in (1200, 6):
        got = evaluate_many(policy, [dict(j) for j in jobs], threads=2, seed=3, max_duels=cap)
        assert [r["episodes"] for r in got] == [12, 9, 6]
        assert all(r["won"] + r["lost"] + r["timed_out"] == r["episodes"] for r in got)
        assert got[0]["decisions"] <= 12 * 20 and got[2]["decisions"] <= 6 * 40
