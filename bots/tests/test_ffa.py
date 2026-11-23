import json
import os
import subprocess
import sys

import numpy as np

from common import diepenv, make_config, tank_id

HERE = os.path.dirname(os.path.abspath(__file__))


def brawl(games=16, players=4, kill=1.0, tanks=None, **kw):
    """Scripted fighters close together in a small arena, so tanks die quickly; every seat is a row."""
    settings = dict(arena_size=2000, spawn_min=300, max_decisions=400, kill_reward=kill, dealt_scale=0, taken_scale=0)
    c = make_config("fighter", **dict(settings, **kw))
    c.players = players
    c.kinds = [diepenv.FIGHTER] * players
    c.expose_scripted = True
    c.tanks = tanks or [tank_id("fighter")] * players
    v = diepenv.Vec(c, games, 4)
    v.seed(1)
    v.reset_all()
    return v


def play(v, steps):
    n = v.rows
    out = []
    for _ in range(steps):
        out.append(v.step(np.zeros(n, np.int32), np.zeros(n, bool), np.full(n, -1, np.int32), np.zeros(n, np.float32)))
    return out


def test_every_tank_is_on_its_own_and_starts_apart():
    c = make_config("self", arena_size=6000, spawn_min=1500)
    c.players = 4
    c.kinds = [diepenv.EXTERNAL] * 4
    v = diepenv.Vec(c, 50, 4)
    v.seed(2)
    v.reset_all()
    assert v.rows == 200 and [s for _, s in v.row_map[:4]] == [0, 1, 2, 3]
    for d in range(50):
        tanks = v.duel_state(d)["tanks"]
        xy = np.array([[t["x"], t["y"]] for t in tanks])
        gaps = [np.hypot(*(xy[i] - xy[j])) for i in range(4) for j in range(i)]
        assert min(gaps) >= 1500
    o = v.observe()
    assert o["alive"].all() and (o["enemy_tank"] == -1).all()
    # Each tank on screen is an enemy, whatever its seat.
    tanks = o["mask"] & (o["ents"][..., 18] == 1)
    assert (o["targetable"] == tanks).all()
    assert (o["self"][:, diepenv.ENEMIES_LEFT] == 1).all()


def test_each_tank_is_paid_for_its_kills_its_death_and_being_last():
    v = brawl(win_reward=1.0, loss_reward=1.0)
    game = np.array([d for d, _ in v.row_map])
    paid = np.zeros(v.rows)
    owed = {"kills": np.zeros(v.rows), "won": np.zeros(v.rows), "lost": np.zeros(v.rows)}
    games = deaths = kills = 0
    for o in play(v, 400):
        paid += o["reward"]
        np.add.at(paid, o["credit_row"], o["credit_value"])
        for k in owed:
            owed[k] += o[k]
        kills += int(o["kills"].sum())
        deaths += int(o["lost"].sum())
        assert not o["died_to_shape"].any(), "with no shapes, every death is some tank's kill"
        over = np.bincount(game, o["game_over"], v.duels) > 0
        # A finished game has every seat over at once, and at most one tank left.
        assert (o["game_over"] == over[game]).all()
        ended = np.flatnonzero(over)
        games += len(ended)
        assert all(np.bincount(game, o["won"], v.duels)[ended] <= 1)
    assert games > 30 and kills == deaths > 60
    assert np.allclose(paid, owed["kills"] + owed["won"] - owed["lost"], atol=1e-4)


def test_dominators_play_at_level_one_however_many_they_kill():
    dominators = [tank_id(n) for n in ("destroyer-dominator", "gunner-dominator", "trapper-dominator")]
    sizes = {}
    for level in (1, 45):
        v = brawl(games=3, tanks=dominators + [tank_id("fighter")], level=level)
        sizes[level] = [t["size"] for t in v.duel_state(0)["tanks"]]
    assert sizes[1][:3] == sizes[45][:3] and sizes[1][3] < sizes[45][3]

    v = brawl(games=12, tanks=[dominators[0]] + [tank_id("fighter")] * 3)
    first = np.flatnonzero(np.array([s for _, s in v.row_map]) == 0)
    start = v.duel_state(0)["tanks"][0]
    kills = 0
    for o in play(v, 400):
        kills += int(o["kills"][first].sum())
        for d in range(v.duels):
            t = v.duel_state(d)["tanks"][0]
            if t.get("health", 0) > 0:
                assert np.allclose([t["size"], t["max_health"]], [start["size"], start["max_health"]])
    assert kills > 5


def test_a_dead_tank_sits_out_the_rest_of_its_game():
    v = brawl()
    out = False
    benched = np.zeros(v.rows, bool)
    for o in play(v, 300):
        assert not (o["done"] & benched).any(), "a dead tank's episode already ended"
        # The observation after a game's last decision is the next game's first.
        over = o["game_over"]
        benched &= ~over
        assert not (o["alive"] & benched).any()
        benched |= o["lost"] & ~over
        out |= bool(benched.any())
        # A dead tank sees nothing.
        assert not o["mask"][~o["alive"]].any()
    assert out


def test_the_game_ends_once_no_learner_is_left():
    c = make_config("fighter", arena_size=2000, spawn_min=300, max_decisions=400)
    c.players = 4
    c.kinds = [diepenv.FIGHTER] * 4
    c.expose_scripted = True
    c.learner = [True, False, False, False]
    v = diepenv.Vec(c, 24, 4)
    v.seed(3)
    v.reset_all()
    first = np.flatnonzero(np.array([s for _, s in v.row_map]) == 0)
    seen = 0
    for o in play(v, 300):
        died = o["lost"][first]
        assert (o["game_over"][first][died]).all()
        seen += int(died.sum())
    assert seen > 5


def test_a_duel_win_pays_the_kill_and_being_last():
    v = brawl(players=2, games=16, win_reward=1.0, loss_reward=1.0)
    paid = np.zeros(v.rows)
    won = np.zeros(v.rows)
    lost = np.zeros(v.rows)
    for o in play(v, 300):
        paid += o["reward"]
        np.add.at(paid, o["credit_row"], o["credit_value"])
        won += o["won"]
        lost += o["lost"]
    assert won.sum() > 20 and np.allclose(paid, 2 * won - lost, atol=1e-4)


def test_remembered_tanks_are_where_they_were_seen_and_cant_be_aimed_at():
    c = make_config("self", arena_size=4000, spawn_min=900, max_decisions=300)
    c.players = 4
    c.kinds = [diepenv.EXTERNAL] * 4
    c.remember = [True, True, False, True]
    v = diepenv.Vec(c, 16, 4)
    v.seed(4)
    v.reset_all()
    slot = np.array([s for _, s in v.row_map])
    rng = np.random.default_rng(0)
    o = v.observe()
    shown = 0
    for _ in range(200):
        e, m = o["ents"], o["mask"]
        ghost = m & (e[..., diepenv.REMEMBERED] == 1)
        shown += int(ghost.sum())
        assert not ghost[slot == 2].any(), "a seat that doesn't remember is never shown one"
        assert not (o["targetable"] & ghost).any()
        assert (e[ghost][:, diepenv.SINCE_SEEN] > 0).all() and (e[m & ~ghost][:, diepenv.SINCE_SEEN] == 0).all()
        assert (e[ghost][:, 18] == 1).all() and (e[ghost][:, 22] == 1).all() and not e[ghost][:, 24:27].any()
        assert (ghost.sum(1) <= 3).all()
        n = v.rows
        o = v.step(rng.integers(0, 9, n).astype(np.int32), np.zeros(n, bool), np.full(n, -1, np.int32),
                   np.zeros(n, np.float32))
    assert shown > 50


def test_a_run_with_free_for_all_games_trains_and_examines_them(tmp_path):
    out = str(tmp_path / "run")
    ref = str(tmp_path / "ref.pt")
    train = os.path.join(HERE, "..", "train")
    base = [sys.executable, "ppo.py", "--iters", "2", "--duels", "4", "--mix", "live:1,self:1", "--rollout", "16",
            "--lag", "4", "--minibatch", "256", "--width", "32", "--layers", "1", "--threads", "4",
            "--classes", "fighter:2,overlord", "--eval-episodes", "0", "--frozen-eval-episodes", "8",
            "--arena", "2000", "--max-decisions", "60"]
    subprocess.run(base + ["--out", out + "0"], cwd=train, check=True, capture_output=True)
    os.replace(os.path.join(out + "0", "it00002.pt"), ref)
    subprocess.run(base + ["--out", out, "--init", ref, "--ffa", "4", "--ffa-arena", "3000", "--ffa-decisions", "80",
                           "--ffa-vs", ref, "--ffa-exam-episodes", "4", "--kill-reward", "1", "--remember",
                           "--frozen-eval-every", "2"], cwd=train, check=True, capture_output=True)
    rows = [json.loads(line) for line in open(os.path.join(out, "log.jsonl"))]
    assert all("ffa" in r and 0 <= r["ffa"]["alive"] <= 1 for r in rows)
    assert rows[-1]["ffa_eval"]["episodes"] >= 4
