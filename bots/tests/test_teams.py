import json
import math
import os
import subprocess
import sys

import numpy as np
import torch

from common import diepenv, make_config, tank_id, NUM_CLASSES
from model import Policy, adapt
from ppo import gae

HERE = os.path.dirname(os.path.abspath(__file__))
TANK, ENEMY, ALLY, REMEMBERED = 18, 22, diepenv.ALLY, diepenv.REMEMBERED


def teams(games=16, size=2, kinds=None, seed=1, **kw):
    """Two teams of fighters, blue's seats first; every seat is a row."""
    settings = dict(arena_size=3000, spawn_min=900, spawn_max=1200, max_decisions=400)
    c = make_config("self", **dict(settings, **kw))
    n = 2 * size
    c.players = n
    c.teams = [0] * size + [1] * size
    c.kinds = kinds or [diepenv.EXTERNAL] * n
    c.expose_scripted = True
    c.tanks = [tank_id("fighter")] * n
    v = diepenv.Vec(c, games, 4)
    v.seed(seed)
    v.reset_all()
    return v


def still(v):
    n = v.rows
    return dict(move=np.zeros(n, np.int32), fire=np.zeros(n, bool), target=np.full(n, -1, np.int32),
                aim=np.zeros(n, np.float32))


def aim_at(v, pick):
    """Every row firing at the absolute angle of the tank in slot pick(slot) of its game."""
    a = still(v)
    a["fire"][:] = True
    for r, (d, s) in enumerate(v.row_map):
        tanks = v.duel_state(d)["tanks"]
        me, them = tanks[s], tanks[pick(s)]
        if "x" not in me or "x" not in them:
            continue
        a["aim"][r] = math.atan2(them["y"] - me["y"], them["x"] - me["x"])
    return a


def test_teammates_start_together_and_the_teams_apart():
    v = teams(games=40, arena_size=6000, spawn_min=2000, spawn_max=4500)
    for d in range(v.duels):
        xy = np.array([[t["x"], t["y"]] for t in v.duel_state(d)["tanks"]])
        assert 300 <= np.hypot(*(xy[0] - xy[1])) <= 600 and 300 <= np.hypot(*(xy[2] - xy[3])) <= 600
        assert 2000 - 1 <= np.hypot(*(xy[:2].mean(0) - xy[2:].mean(0))) <= 4500 + 1


def test_teammates_cant_hurt_each_other():
    # Far enough from the other team that shots going past a teammate never reach it.
    v = teams(arena_size=8000, spawn_min=5000, spawn_max=5500)
    taken = dealt = 0.0
    for _ in range(60):
        o = v.step(**aim_at(v, lambda s: s ^ 1))
        taken += float(o["taken"].sum())
        dealt += float(o["dealt"].sum())
    assert taken == 0 and dealt == 0
    # Fired at the other team instead, the same shots land.
    v = teams()
    for _ in range(60):
        o = v.step(**aim_at(v, lambda s: (s + 2) % 4))
        taken += float(o["taken"].sum())
    assert taken > 1


def test_the_last_team_left_wins_and_everyone_is_paid_their_share():
    kinds = [diepenv.FIGHTER] * 4
    v = teams(games=24, kinds=kinds, arena_size=2000, spawn_min=500, spawn_max=700, kill_reward=1.0, win_reward=1.0,
              loss_reward=0.5, eliminated_reward=0.5, dealt_scale=0, taken_scale=0)
    game = np.array([d for d, _ in v.row_map])
    slot = np.array([s for _, s in v.row_map])
    side = slot // 2
    paid, owed = np.zeros(v.rows), np.zeros(v.rows)
    games = timeouts = kills = deaths = handoffs = 0
    for _ in range(400):
        o = v.step(**still(v))
        paid += o["reward"]
        np.add.at(paid, o["credit_row"], o["credit_value"])
        owed += o["kills"] + o["mate_kills"] + o["won"] - 0.5 * o["lost"] - 0.5 * o["eliminated"]
        kills += int(o["kills"].sum())
        deaths += int(o["lost"].sum())
        mid = o["lost"] & ~o["game_over"]
        # A tank that dies with its teammate alive hands its future on to it, and the last of a team to die doesn't.
        assert (o["handoff"][mid & ~o["eliminated"]] == slot[mid & ~o["eliminated"]] ^ 1).all()
        assert (o["handoff"][o["eliminated"] | o["game_over"]] == -1).all()
        handoffs += int((o["handoff"] >= 0).sum())
        for d in np.flatnonzero(np.bincount(game, o["game_over"], v.duels) > 0):
            rows = np.flatnonzero(game == d)
            games += 1
            if o["timed_out"][rows].any() or not o["team_lost"][rows].any():
                timeouts += 1
                continue
            # One team won, and of it only the tanks still alive got the win.
            won = o["team_won"][rows]
            assert won.sum() == 2 and len(set(side[rows][won])) == 1
            assert (o["team_lost"][rows] == ~won).all()
            assert o["won"][rows].sum() == o["team_left"][rows][won][0] > 0
            assert not o["won"][rows][~won].any()
    assert games > 30 and timeouts < games / 2
    assert kills == deaths > 60 and handoffs > 10
    assert np.allclose(paid, owed, atol=1e-4)


def test_a_teammate_is_an_ally_on_screen_and_when_remembered():
    c_kw = dict(arena_size=6000, spawn_min=3000, spawn_max=3500, max_decisions=200)
    v = teams(games=16, remember=[True] * 4, **c_kw)
    rng = np.random.default_rng(0)
    # Each tank keeps going one way, so teammates drift apart and out of sight.
    heading = rng.integers(1, 9, v.rows).astype(np.int32)
    seen = remembered = 0
    o = v.observe()
    assert np.allclose(o["self"][:, diepenv.MATES], 1 / 3) and np.allclose(o["self"][:, diepenv.MATES_LEFT], 1 / 3)
    for _ in range(150):
        e, m = o["ents"], o["mask"]
        ally = m & (e[..., ALLY] == 1)
        tank = m & (e[..., TANK] == 1)
        assert not (ally & (e[..., ENEMY] == 1)).any() and not (o["targetable"] & ally).any()
        assert not (m & (e[..., ALLY] == 1) & (e[..., 21] == 1)).any(), "my own bullets are mine, not an ally's"
        assert ((ally & tank).sum(1) <= 1).all()
        seen += int((ally & tank & (e[..., REMEMBERED] == 0)).sum())
        remembered += int((ally & tank & (e[..., REMEMBERED] == 1)).sum())
        a = still(v)
        a["move"] = heading
        o = v.step(**a)
    assert seen > 100 and remembered > 100


def test_a_free_for_all_has_no_allies():
    c = make_config("self", arena_size=3000, spawn_min=600, max_decisions=100)
    c.players = 4
    c.kinds = [diepenv.EXTERNAL] * 4
    c.remember = [True] * 4
    v = diepenv.Vec(c, 8, 4)
    v.seed(5)
    v.reset_all()
    rng = np.random.default_rng(1)
    o = v.observe()
    for _ in range(60):
        assert not o["ents"][..., ALLY].any()
        assert not o["self"][:, diepenv.MATES].any() and not o["self"][:, diepenv.MATES_LEFT].any()
        n = v.rows
        o = v.step(rng.integers(0, 9, n).astype(np.int32), np.ones(n, bool), np.full(n, -1, np.int32),
                   rng.uniform(-3, 3, n).astype(np.float32))
        assert (o["handoff"] == -1).all() and not o["mate_kills"].any() and not o["same_target"].any()


def test_a_dead_tanks_return_carries_on_as_its_teammates():
    gamma = 0.9
    reward = torch.tensor([[0.0, 0.0], [-0.5, 0.0], [0.0, 0.0], [0.0, 1.0]])
    done = torch.tensor([[False, False], [True, False], [False, False], [False, False]])
    value = torch.zeros(4, 2)
    mate = torch.tensor([[-1, -1], [1, -1], [-1, -1], [-1, -1]])
    _, ret = gae(reward, value, done, torch.zeros(2), gamma, 1.0, mate)
    assert torch.allclose(ret[0, 0], torch.tensor(-0.5 * gamma + gamma ** 3))
    _, alone = gae(reward, value, done, torch.zeros(2), gamma, 1.0)
    assert torch.allclose(alone[0, 0], torch.tensor(-0.5 * gamma))
    assert torch.allclose(ret[:, 1], alone[:, 1])


def test_a_policy_from_before_teams_plays_them_as_it_did():
    torch.manual_seed(0)
    old = Policy(d=32, layers=1, self_dim=diepenv.ENEMIES_LEFT + 1, ent_dim=ALLY, classes=NUM_CLASSES, any_class=True)
    new = Policy(d=32, layers=1, self_dim=diepenv.SELF_DIM, ent_dim=diepenv.ENT_DIM, classes=NUM_CLASSES, any_class=True)
    new.load_state_dict(adapt(old.state_dict(), new))
    v = teams(games=4)
    o = v.observe()
    obs = {k: torch.from_numpy(o[k]) for k in ("self", "ents", "mask", "targetable", "tank")}
    assert obs["ents"][..., ALLY].any()
    a, _ = old(obs)
    b, _ = new(obs)
    for k in ("move", "fire", "target", "value"):
        assert torch.allclose(a[k], b[k], atol=1e-5)


def test_a_run_with_team_games_trains_and_examines_them(tmp_path):
    out = str(tmp_path / "run")
    ref = str(tmp_path / "ref.pt")
    train = os.path.join(HERE, "..", "train")
    base = [sys.executable, "ppo.py", "--iters", "2", "--duels", "4", "--mix", "live:1,self:1", "--rollout", "16",
            "--lag", "4", "--minibatch", "256", "--width", "32", "--layers", "1", "--threads", "4",
            "--classes", "fighter,overlord", "--eval-episodes", "0", "--frozen-eval-episodes", "8",
            "--arena", "2000", "--max-decisions", "60"]
    subprocess.run(base + ["--out", out + "0"], cwd=train, check=True, capture_output=True)
    os.replace(os.path.join(out + "0", "it00002.pt"), ref)
    subprocess.run(base + ["--out", out, "--init", ref, "--team", "4", "--team-arena", "3000", "--team-decisions", "80",
                           "--team-spawn-min", "800", "--team-spawn-max", "1200", "--team-vs", ref,
                           "--team-exam-every", "2", "--team-exam-episodes", "4", "--kill-reward", "1", "--remember"],
                   cwd=train, check=True, capture_output=True)
    rows = [json.loads(line) for line in open(os.path.join(out, "log.jsonl"))]
    assert all("team" in r and 0 <= r["team"]["together"] <= 1 for r in rows)
    assert rows[-1]["team_eval"]["episodes"] >= 4
