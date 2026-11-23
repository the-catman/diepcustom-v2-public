import json
import math
import subprocess
import sys

import numpy as np
import torch

from common import diepenv, make_config, tank_id
from model import Policy, sample, log_prob, env_action
from test_env import idle, target_row
from test_resume import SMALL, TRAIN

OVERSEER = tank_id("overseer")
OBS_KEYS = ("self", "ents", "mask", "targetable")


def duel(opponent="still", duels=1, **kw):
    v = diepenv.Vec(make_config(opponent, tanks=[OVERSEER, 0], spawn_min=900, spawn_max=900, **kw), duels, 1)
    v.seed(3)
    v.reset_all()
    return v


def drive(v, steps, fire, repel, reach, targeted=True):
    """Holds the same clicks and mouse for steps decisions. Returns damage dealt and taken, and the credits."""
    n = v.rows
    dealt = taken = 0.0
    credits = 0
    o = v.observe()
    for _ in range(steps):
        move, _, target, aim = idle(n)
        if targeted:
            target = np.array([target_row(o, r) for r in range(n)], np.int32)
        o = v.step(move, np.full(n, fire), target, aim, repel=np.full(n, repel), reach=np.full(n, reach, np.float32))
        dealt += float(o["dealt"].sum())
        taken += float(o["taken"].sum())
        credits += len(o["credit_row"])
    return dealt, taken, credits


def own_drones(v):
    return [r for r in v.duel_entities(0) if r[3] == 2 and r[4] == 0]


def test_drones_attack_a_mouse_put_on_the_target():
    on_target, _, _ = drive(duel(duels=4), 120, True, False, 0.0)
    # The old far-away mouse sends them off past the target.
    far, _, _ = drive(duel(duels=4), 120, True, False, math.nan)
    assert on_target > 0.5
    assert on_target > 4 * far


def test_repel_sends_drones_away_from_the_mouse():
    def spread(repel):
        v = duel()
        drive(v, 20, not repel, repel, 0.0)
        enemy = v.duel_state(0)["tanks"][1]
        return np.mean([math.hypot(r[0] - enemy["x"], r[1] - enemy["y"]) for r in own_drones(v)])
    assert spread(True) > spread(False) + 500


def test_drone_hits_are_paid_on_impact():
    dealt, _, credits = drive(duel(duels=4), 120, True, False, 0.0)
    assert dealt > 0
    assert credits == 0


def test_the_mouse_goes_where_reach_says():
    v = duel()
    me = v.duel_state(0)["tanks"][0]

    def mouse_distance(reach, targeted):
        drive(v, 1, False, False, reach, targeted)
        t = v.duel_state(0)["tanks"][0]
        return math.hypot(t["mouse_x"] - t["x"], t["mouse_y"] - t["y"])

    assert abs(mouse_distance(math.nan, True) - 10000) < 50
    assert abs(mouse_distance(math.log(500), False) - 500) < 5
    assert abs(mouse_distance(0.0, True) - 900) < 20
    assert abs(mouse_distance(math.log(0.5), True) - 450) < 20
    # Never on the tank's own centre.
    assert mouse_distance(math.log(1e-3), False) >= me["size"] - 1


def test_observations_mark_drones():
    v = duel()
    drive(v, 20, False, False, math.nan, False)
    ents = v.observe()["ents"][0]
    mine = ents[(ents[:, 19] == 1) & (ents[:, 21] == 1)]
    assert len(mine) > 0 and (mine[:, 20] == 1).all()
    # Ordinary bullets stay unmarked.
    b = diepenv.Vec(make_config("still", spawn_min=900, spawn_max=900), 1, 1)
    move, fire, target, aim = idle(1)
    for _ in range(10):
        o = b.step(move, np.ones(1, bool), target, aim)
    bullets = o["ents"][0][o["ents"][0][:, 19] == 1]
    assert len(bullets) > 0 and (bullets[:, 20] == 0).all()


def test_the_scripted_fighter_can_play_an_overseer():
    v = diepenv.Vec(make_config("fighter", tanks=[0, OVERSEER], spawn_min=900, spawn_max=900), 4, 1)
    v.seed(3)
    v.reset_all()
    taken = 0.0
    for _ in range(120):
        o = v.step(*idle(4))
        taken += float(o["taken"].sum())
    assert taken > 0.5


def tensors(obs):
    return {k: torch.from_numpy(obs[k]) for k in OBS_KEYS}


def test_drone_heads_leave_the_other_actions_alone():
    plain = Policy(d=32, heads=2, layers=1)
    assert not any(k.startswith(("repel", "reach", "free_reach")) for k in plain.state_dict())
    drones = Policy(d=32, heads=2, layers=1, drones=True)
    drones.load_state_dict(plain.state_dict(), strict=False)
    obs = tensors(duel(duels=8).observe())
    torch.manual_seed(5)
    a, _ = plain(obs)
    a = sample(a)
    torch.manual_seed(5)
    b, _ = drones(obs)
    b = sample(b)
    assert "repel" not in a and "repel" in b
    for k in a:
        assert torch.equal(a[k], b[k])


def test_untrained_drone_mouse_sits_on_the_target():
    policy = Policy(d=32, heads=2, layers=1, drones=True)
    out, _ = policy(tensors(duel(duels=4).observe()))
    assert torch.allclose(out["reach"], torch.zeros_like(out["reach"]))
    assert (torch.sigmoid(out["repel"]) < 0.5).all()


def test_mouse_distance_only_counts_while_clicking():
    torch.manual_seed(0)
    policy = Policy(d=32, heads=2, layers=1, drones=True)
    out, _ = policy(tensors(duel(duels=8).observe()))
    a = sample(out)
    moved = dict(a, reach=a["reach"] + 1)
    for fire, repel in [(False, False), (True, False), (False, True)]:
        a["fire"][:], a["repel"][:] = fire, repel
        moved["fire"], moved["repel"] = a["fire"], a["repel"]
        same = torch.allclose(log_prob(out, a), log_prob(out, moved))
        assert same == (not fire and not repel)
    assert set(env_action(a)) == {"move", "fire", "target", "aim", "repel", "reach"}


def test_an_overseer_trains(tmp_path):
    out = tmp_path / "run"
    r = subprocess.run([sys.executable, "ppo.py", "--out", str(out), *SMALL, "--iters", "2", "--tank", "overseer"],
                       cwd=TRAIN, capture_output=True, text=True)
    assert r.returncode == 0, r.stderr
    rows = [json.loads(line) for line in open(out / "log.jsonl")]
    assert set(rows[-1]["mouse"]) >= {"attract", "repel", "ratio", "free_reach"}
    ckpt = torch.load(out / "latest.pt", weights_only=False)
    assert ckpt["model_args"]["drones"] is True


def test_drone_tanks_start_with_their_drones_out():
    v = duel()
    me = v.duel_state(0)["tanks"][0]
    drones = own_drones(v)
    assert len(drones) == 8
    for r in drones:
        assert abs(math.hypot(r[0] - me["x"], r[1] - me["y"]) - 250) < 5
    assert math.hypot(me["vx"], me["vy"]) < 1e-6
    off = duel(stock_drones=False)
    assert own_drones(off) == []


def test_training_against_frozen_bots_of_another_class(tmp_path):
    sniper = tmp_path / "sniper"
    r = subprocess.run([sys.executable, "ppo.py", "--out", str(sniper), *SMALL, "--iters", "1", "--tank", "sniper"],
                       cwd=TRAIN, capture_output=True, text=True)
    assert r.returncode == 0, r.stderr
    bot = str(sniper / "it00001.pt")
    out = tmp_path / "run"
    r = subprocess.run([sys.executable, "ppo.py", "--out", str(out), *SMALL, "--iters", "2", "--tank", "overseer",
                        "--mix", "strafe:1,self:1,frozen:2", "--frozen", f"{bot}:2", "--holdout", bot,
                        "--frozen-eval-every", "2", "--frozen-eval-episodes", "4"],
                       cwd=TRAIN, capture_output=True, text=True)
    assert r.returncode == 0, r.stderr
    rows = [json.loads(line) for line in open(out / "log.jsonl")]
    label = "sniper/it00001"
    assert all(row[label]["episodes"] > 0 or row["iter"] == 1 for row in rows)
    assert set(rows[-1]) >= {"strafe", "self", label, "frozen_eval"}
    assert rows[-1]["frozen_eval"][label]["episodes"] >= 4
    # A frozen bot can't be one that's still being written.
    r = subprocess.run([sys.executable, "ppo.py", "--out", str(tmp_path / "bad"), *SMALL, "--iters", "1",
                        "--mix", "frozen:1", "--frozen", str(sniper / "latest.pt")],
                       cwd=TRAIN, capture_output=True, text=True)
    assert r.returncode != 0 and "numbered checkpoint" in r.stderr


def factory_duel(cap):
    v = diepenv.Vec(make_config("fighter", tanks=[tank_id("factory")] * 2, spawn_min=700, spawn_max=700,
                                entity_cap=cap), 4, 1)
    v.seed(3)
    v.reset_all()
    return v


def test_the_entity_cap_limits_what_a_tank_is_shown():
    shown = {}
    for cap in (40, 256):
        v = factory_duel(cap)
        most = seen = 0
        for _ in range(60):
            o = v.step(*idle(4), repel=np.zeros(4, bool), reach=np.zeros(4, np.float32))
            most = max(most, int(o["mask"].sum(1).max()))
            seen = max(seen, int(o["seen"].max()))
            if cap == 256:
                assert (o["mask"].sum(1) == o["seen"]).all()
        shown[cap] = most
        assert seen > 40
    assert shown[40] == 40 and shown[256] > 40


def test_factory_minion_hits_are_paid_on_impact():
    v = diepenv.Vec(make_config("still", tanks=[tank_id("factory"), 0], spawn_min=700, spawn_max=700), 4, 1)
    v.seed(3)
    v.reset_all()
    dealt, _, credits = drive(v, 120, True, False, 0.0)
    assert dealt > 0 and credits == 0


def test_factories_start_with_minions_and_battleships_with_nothing():
    f = diepenv.Vec(make_config("still", tanks=[tank_id("factory"), 0]), 1, 1)
    assert len([r for r in f.duel_entities(0) if r[3] == 2 and r[4] == 0]) == 6
    b = diepenv.Vec(make_config("still", tanks=[tank_id("battleship"), 0]), 1, 1)
    assert len([r for r in b.duel_entities(0) if r[3] > 0]) == 0
