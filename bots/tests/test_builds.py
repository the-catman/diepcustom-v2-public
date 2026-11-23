import subprocess
import sys
from types import SimpleNamespace

import pytest
import torch

from common import diepenv, make_config, parse_build, build_text, builds_of, DEFAULT_BUILD
from ppo import build_envs
from test_resume import SMALL, TRAIN

GLASS = "0/0/0/7/7/7/7/5"
TANKY = "0/7/0/5/6/2/7/5"


def max_health(v, duel, slot):
    return v.duel_state(duel)["tanks"][slot]["max_health"]


def test_builds_are_written_in_diep_order():
    assert parse_build(GLASS) == [5, 7, 7, 7, 7, 0, 0, 0]
    assert build_text(parse_build(GLASS)) == GLASS
    assert builds_of({}) == [parse_build(DEFAULT_BUILD)] * 2
    for bad in ("0/0/0/7/7/7/7", "0/0/0/7/7/7/7/8", "7/7/7/7/7/7/7/7"):
        with pytest.raises(ValueError):
            parse_build(bad)


def test_each_slot_has_its_own_build():
    c = make_config("still", stats=[parse_build(TANKY), parse_build(GLASS)])
    assert c.stats == [parse_build(TANKY), parse_build(GLASS)]
    v = diepenv.Vec(c, 1, 1)
    v.reset_all()
    assert max_health(v, 0, 0) > max_health(v, 0, 1)
    c.stats = parse_build(GLASS)
    assert c.stats == [parse_build(GLASS)] * 2
    v = diepenv.Vec(c, 1, 1)
    v.reset_all()
    assert max_health(v, 0, 0) == max_health(v, 0, 1)


def test_frozen_bots_keep_their_own_build():
    args = SimpleNamespace(mix="strafe:1,frozen:1", duels=2, threads=1)
    env_args = dict(tanks=[0, 0], stats=[parse_build(TANKY)] * 2)
    frozen = [{"tank": 0, "stats": parse_build(GLASS), "weight": 1, "label": "glass", "policy": None}]
    vec, _, row_kind, *_ = build_envs(args, env_args, 1, frozen)
    assert list(row_kind) == ["strafe", "glass"]
    # The scripted opponent mirrors the learner; the frozen one plays the build it trained with.
    assert max_health(vec, 0, 1) == max_health(vec, 0, 0)
    assert max_health(vec, 1, 1) < max_health(vec, 1, 0)


def test_a_run_saves_its_build(tmp_path):
    old = tmp_path / "old"
    r = subprocess.run([sys.executable, "ppo.py", "--out", str(old), *SMALL, "--iters", "1"],
                       cwd=TRAIN, capture_output=True, text=True)
    assert r.returncode == 0, r.stderr
    new = tmp_path / "new"
    r = subprocess.run([sys.executable, "ppo.py", "--out", str(new), *SMALL, "--iters", "1", "--build", GLASS,
                        "--mix", "fighter:1,frozen:1", "--frozen", str(old / "it00001.pt")],
                       cwd=TRAIN, capture_output=True, text=True)
    assert r.returncode == 0, r.stderr
    assert f"build {GLASS}, old/it00001 {DEFAULT_BUILD}" in r.stdout
    env = torch.load(new / "latest.pt", weights_only=False)["env_args"]
    assert builds_of(env) == [parse_build(GLASS)] * 2
