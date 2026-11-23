import json
import subprocess
import sys
from collections import Counter
from types import SimpleNamespace

import numpy as np
import torch

from common import diepenv, make_config, tank_id, parse_build, build_for, NUM_CLASSES, OLD_SELF_DIM, OLD_ENT_DIM
from model import Policy, sample, log_prob, env_action, adapt
from ppo import build_envs, choices, pfsp_weights
from test_resume import SMALL, TRAIN

OBS_KEYS = ("self", "ents", "mask", "targetable", "tank")
GLASS = parse_build("0/0/0/5/7/7/7/7")
TANKY = parse_build("0/7/0/5/6/2/7/5")


def tensors(obs):
    return {k: torch.from_numpy(obs[k]) for k in OBS_KEYS}


def any_class(**kw):
    return Policy(d=32, heads=2, layers=1, self_dim=diepenv.SELF_DIM, ent_dim=diepenv.ENT_DIM, classes=NUM_CLASSES,
                  any_class=True, **kw)


def pair(a, b, **kw):
    c = make_config("self", spawn_min=700, spawn_max=700, **kw)
    c.tanks = [tank_id(a), tank_id(b)]
    v = diepenv.Vec(c, 2, 1)
    v.seed(4)
    v.reset_all()
    return v


def test_each_tank_sees_its_own_class_and_the_enemys():
    v = pair("fighter", "overlord")
    o = v.observe()
    assert list(o["tank"]) == [tank_id("fighter"), tank_id("overlord")] * 2
    assert list(o["enemy_tank"]) == [tank_id("overlord"), tank_id("fighter")] * 2
    mine = o["self"][:, OLD_SELF_DIM:diepenv.ENEMIES_LEFT]
    # Only the Overlord's drones follow its mouse.
    assert list(mine[:, 0]) == [0, 1, 0, 1]
    for row, other in ((0, 1), (1, 0)):
        enemy = np.flatnonzero(o["targetable"][row])
        assert len(enemy) == 1
        assert np.array_equal(o["ents"][row, enemy[0], OLD_ENT_DIM:diepenv.CLASS_ENT_DIM], mine[other])
    # Bullets and drones carry no class features.
    rest = o["mask"][1] & ~o["targetable"][1]
    assert rest.any() and not o["ents"][1][rest][:, OLD_ENT_DIM:].any()


def test_class_features_tell_classes_apart_and_ignore_turning():
    seen = {}
    for cls in ("fighter", "penta-shot", "spread-shot", "octo-tank", "ranger", "factory"):
        v = pair(cls, "tank", symmetry=True)
        frames = []
        for f in range(4):
            v.set_frame(0, 0, f)
            frames.append(v.observe()["self"][0, OLD_SELF_DIM:diepenv.ENEMIES_LEFT].copy())
        assert all(np.array_equal(frames[0], x) for x in frames)
        seen[cls] = frames[0]
    names = list(seen)
    for i in range(len(names)):
        for j in range(i + 1, len(names)):
            assert not np.array_equal(seen[names[i]], seen[names[j]]), (names[i], names[j])
    # Firepower by direction adds up to 1; an Octo Tank's is the same all round.
    aim = seen["octo-tank"][16:24]
    assert abs(aim.sum() - 1) < 1e-5 and np.allclose(aim, aim[0])
    assert seen["fighter"][16] > seen["fighter"][18] > seen["fighter"][20]


def test_games_deal_classes_and_builds_by_weight():
    c = make_config("self")
    c.choices = [[(tank_id("fighter"), TANKY, 1.0), (tank_id("penta-shot"), GLASS, 3.0)],
                 [(tank_id("octo-tank"), GLASS, 1.0)]]
    v = diepenv.Vec(c, 400, 2)
    v.seed(9)
    v.reset_all()
    first = Counter(v.duel_state(d)["tanks"][0]["tank"] for d in range(400))
    assert set(first) == {tank_id("fighter"), tank_id("penta-shot")}
    assert 0.65 < first[tank_id("penta-shot")] / 400 < 0.85
    assert all(v.duel_state(d)["tanks"][1]["tank"] == tank_id("octo-tank") for d in range(400))
    health = {}
    for d in range(400):
        t = v.duel_state(d)["tanks"][0]
        health[t["tank"]] = t["max_health"]
    assert health[tank_id("fighter")] > health[tank_id("penta-shot")]
    o = v.observe()
    assert set(o["tank"][::2]) == set(first)


def test_frozen_games_deal_only_the_learner():
    args = SimpleNamespace(mix="strafe:1,frozen:1,live:1", duels=3, threads=1)
    env_args = dict(tanks=[0, 0], stats=[GLASS] * 2)
    frozen = [{"tank": tank_id("sniper"), "stats": GLASS, "weight": 1, "label": "sniper", "policy": None}]
    builds = {t: build_for(t) for t in (tank_id("fighter"), tank_id("twin"))}
    mine, theirs = choices([(tank_id("fighter"), 1)], builds), choices([(tank_id("twin"), 1)], builds)
    vec, _, row_kind, learner, opponent, opp_kind, dealt, far, _ = build_envs(args, env_args, 1, frozen, None, mine, theirs)
    tanks = [[t["tank"] for t in vec.duel_state(d)["tanks"]] for d in range(3)]
    assert tanks == [[tank_id("fighter"), tank_id("twin")], [tank_id("fighter"), tank_id("sniper")],
                     [tank_id("fighter"), tank_id("twin")]]
    # Both tanks of a live game learn; the frozen bot's row is an opponent.
    assert list(row_kind) == ["strafe", "sniper", "live", "live"]
    assert list(opp_kind) == ["sniper"]
    assert [d for d, _ in dealt] == [0, 2] and [d for d, _ in far] == [2]


def test_an_old_bot_fitted_to_the_any_class_policy_plays_the_same():
    torch.manual_seed(0)
    old = Policy(d=32, heads=2, layers=1, self_dim=OLD_SELF_DIM)
    new = any_class()
    new.load_state_dict(adapt(old.state_dict(), new))
    v = pair("fighter", "penta-shot")
    obs = tensors(v.observe())
    a, _ = old(obs)
    b, _ = new(obs)
    for k in a:
        assert torch.allclose(a[k], b[k], atol=1e-6), k
    assert not b["mouse"].any()


def test_only_tanks_with_mouse_drones_use_the_drone_controls():
    torch.manual_seed(1)
    policy = any_class()
    # Make right clicking likely, so a gate that leaks would show.
    torch.nn.init.constant_(policy.repel.bias, 3.0)
    v = pair("fighter", "overseer")
    obs = tensors(v.observe())
    out, _ = policy(obs)
    mouse = out["mouse"]
    assert list(mouse) == [False, True, False, True]
    a = sample(out)
    assert not a["repel"][~mouse].any() and a["repel"][mouse].all()
    e = env_action(a)
    assert torch.isnan(e["reach"][~mouse]).all() and torch.isfinite(e["reach"][mouse]).all()
    # What a bullet tank's drone controls say doesn't change how likely its action was.
    lp = log_prob(out, a)
    b = dict(a, repel=torch.ones_like(a["repel"]), reach=a["reach"] + 5)
    changed = log_prob(out, b)
    assert torch.equal(lp[~mouse], changed[~mouse]) and not torch.equal(lp[mouse], changed[mouse])
    assert torch.isfinite(lp).all()
    # The gradient stays finite too.
    policy.zero_grad()
    out, _ = policy(obs)
    log_prob(out, a).sum().backward()
    assert all(torch.isfinite(p.grad).all() for p in policy.parameters() if p.grad is not None)


def test_pfsp_favours_what_the_focus_class_loses_to():
    classes = [(1, 1.0), (2, 1.0), (3, 1.0), (4, 1.0), (5, 1.0)]
    results = {1: {"loss": 0.9, "draw": 0.0}, 2: {"loss": 0.1, "draw": 0.0}, 3: {"loss": 0.1, "draw": 0.6},
               4: {"loss": 0.0, "draw": 0.0}, 5: None}
    w = pfsp_weights(results, classes)
    assert abs(sum(w.values()) - 1) < 1e-9
    assert abs(w[1] - 0.25) < 1e-9 and max(w.values()) < 0.25 + 1e-9
    assert w[3] > w[2] > w[4] > 0
    # Two classes can't be held to a quarter each.
    w = pfsp_weights({1: {"loss": 1.0, "draw": 0.0}, 2: {"loss": 0.0, "draw": 0.0}}, classes[:2])
    assert abs(w[1] - 0.5) < 1e-9 and abs(w[2] - 0.5) < 1e-9


def test_one_policy_trains_as_many_classes(tmp_path):
    bot = tmp_path / "twin"
    r = subprocess.run([sys.executable, "ppo.py", "--out", str(bot), *SMALL, "--iters", "1", "--tank", "twin",
                        "--last-seen"], cwd=TRAIN, capture_output=True, text=True)
    assert r.returncode == 0, r.stderr
    ckpt = str(bot / "it00001.pt")
    out = tmp_path / "run"
    r = subprocess.run([sys.executable, "ppo.py", "--out", str(out), *SMALL, "--iters", "2",
                        "--classes", "fighter:2,penta-shot,overseer", "--opp-classes", "twin,overseer,fighter",
                        "--mix", "fighter:1,self:1,live:2,frozen:1", "--frozen", ckpt, "--mirror", ckpt, "--pfsp",
                        "--init", ckpt, "--frozen-eval-every", "2", "--frozen-eval-episodes", "4",
                        "--max-decisions", "12"],
                       cwd=TRAIN, capture_output=True, text=True)
    assert r.returncode == 0, r.stderr
    rows = [json.loads(line) for line in open(out / "log.jsonl")]
    last = rows[-1]
    # A live game's other tank learns too, as the class dealt to the opponent.
    assert set(last["as"]) <= {"Fighter", "Penta Shot", "Overseer", "Twin"} and last["as"]
    assert set(last["focus_vs"]) <= {"Twin", "Overseer", "Fighter"}
    assert set(last["pfsp"]) == {"Twin", "Overseer", "Fighter"}
    assert set(last["field_eval"]) == {"Twin", "Overseer", "Fighter"}
    assert set(last["mirror_eval"]) == {"twin/it00001"}
    assert set(last["time"]) == {"policy", "opponents", "env"}
    saved = torch.load(out / "latest.pt", weights_only=False)
    assert saved["model_args"]["any_class"] and saved["env_args"]["tanks"] == [tank_id("fighter")] * 2
    # It carries on where it stopped, dealing what it was.
    r = subprocess.run([sys.executable, "ppo.py", "--out", str(out), "--resume", "--iters", "3"],
                       cwd=TRAIN, capture_output=True, text=True)
    assert r.returncode == 0, r.stderr
    # And it can be watched and played as any class.
    r = subprocess.run([sys.executable, "evaluate.py", str(out / "latest.pt"), "--tank", "overseer", "--opponent",
                        str(out / "latest.pt"), "--opp-tank", "penta-shot", "--episodes", "4", "--duels", "4"],
                       cwd=TRAIN, capture_output=True, text=True)
    assert r.returncode == 0, r.stderr
