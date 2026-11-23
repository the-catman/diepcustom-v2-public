import os
import struct
import sys

import numpy as np
import torch

import evaluate
from common import diepenv, make_config, tank_id
from evaluate import act
from model import Policy

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "live"))
import brain  # noqa: E402


def observations(games=4):
    """Fighters in 2v2s, a few decisions in, so screens hold tanks, bullets and allies."""
    c = make_config("self", arena_size=3000, spawn_min=900, spawn_max=1200, max_decisions=400)
    c.players = 4
    c.teams = [0, 0, 1, 1]
    c.kinds = [diepenv.EXTERNAL] * 4
    c.tanks = [tank_id("fighter")] * 4
    v = diepenv.Vec(c, games, 2)
    v.seed(3)
    v.reset_all()
    n = v.rows
    for _ in range(6):
        v.step(move=np.ones(n, np.int32), fire=np.ones(n, bool), target=np.full(n, -1, np.int32),
               aim=np.zeros(n, np.float32))
    return v.observe()


def obs_frame(seq, rows, obs):
    """What bots-server sends: the rows, then each array, every row padded to the widest."""
    n, width = obs["mask"].shape
    body = bytearray(struct.pack("<IHH", seq, n, width))
    for r in rows:
        body += brain.ROW.pack(*r)
    body += obs["self"].astype(np.float32).tobytes()
    body += obs["ents"].astype(np.float32).tobytes()
    body += obs["mask"].astype(np.uint8).tobytes()
    body += obs["targetable"].astype(np.uint8).tobytes()
    return bytes(body)


def test_the_brain_reads_what_the_server_sends_and_plays_as_exams_do(monkeypatch):
    monkeypatch.setattr(evaluate, "DEVICE", "cpu")
    obs = observations()
    n = len(obs["mask"])
    torch.manual_seed(0)
    policy = Policy(self_dim=diepenv.SELF_DIM, ent_dim=diepenv.ENT_DIM, classes=56, any_class=True)
    policy.eval()
    rows = [(7, i % 4, 0, int(obs["tank"][i]), brain.DETERMINISTIC) for i in range(n)]
    seq, got_rows, got = brain.parse_obs(obs_frame(41, rows, obs))
    assert seq == 41 and got_rows == rows
    for k in ("self", "ents", "mask", "targetable", "tank"):
        assert np.array_equal(got[k], obs[k].astype(got[k].dtype)), k

    bot = brain.Bot.__new__(brain.Bot)
    bot.policy, bot.device, bot.specialists = policy, "cpu", {}
    actions = brain.Brain([bot]).decide(got_rows, got)
    want, _ = act(policy, {k: obs[k] for k in ("self", "ents", "mask", "targetable", "tank")}, True)
    for i, (move, fire, repel, target, aim, reach) in enumerate(actions):
        assert (move, bool(fire), bool(repel), target) == (want["move"][i], want["fire"][i], want["repel"][i],
                                                          want["target"][i])
        assert aim == want["aim"][i]
        assert np.isnan(reach) == np.isnan(want["reach"][i])


def test_a_specialist_plays_its_class_and_the_bot_every_other(monkeypatch):
    monkeypatch.setattr(evaluate, "DEVICE", "cpu")
    obs = observations()
    fighter, overlord = tank_id("fighter"), tank_id("overlord")
    rows = [(7, i % 4, 0, overlord if i % 2 else fighter, brain.DETERMINISTIC) for i in range(len(obs["mask"]))]
    _, rows, got = brain.parse_obs(obs_frame(1, rows, obs))
    torch.manual_seed(0)
    plain, special = (Policy(self_dim=diepenv.SELF_DIM, ent_dim=diepenv.ENT_DIM, classes=56, any_class=True).eval()
                      for _ in range(2))
    bot = brain.Bot.__new__(brain.Bot)
    bot.policy, bot.device, bot.specialists = plain, "cpu", {overlord: ("special.pt", special)}
    actions = brain.Brain([bot]).decide(rows, got)
    keys = ("self", "ents", "mask", "targetable", "tank")
    for policy, mine in ((special, got["tank"] == overlord), (plain, got["tank"] != overlord)):
        idx = np.flatnonzero(mine)
        want, _ = act(policy, {k: got[k][idx] for k in keys}, True)
        assert [actions[i][4] for i in idx] == list(want["aim"])
    idx = np.flatnonzero(got["tank"] == overlord)
    other, _ = act(plain, {k: got[k][idx] for k in keys}, True)
    assert [actions[i][4] for i in idx] != list(other["aim"])


def test_an_answer_packs_and_hello_lists_every_tank():
    body = brain.encode_act(9, [(3, True, False, 2, 0.25, float("nan")), (0, False, True, -1, -1.5, 1.0)])
    seq, n = struct.unpack_from("<IH", body)
    assert (seq, n) == (9, 2)
    first = brain.ACTION.unpack_from(body, 6)
    assert first[:6] == (3, 1, 0, 0, 2, 0) and first[6] == 0.25 and np.isnan(first[7])
    assert brain.ACTION.unpack_from(body, 6 + brain.ACTION.size)[4] == -1

    hello = brain.hello([])
    version, self_dim, ent_dim, bots = struct.unpack_from("<IHHB", hello)
    assert (version, self_dim, ent_dim, bots) == (brain.VERSION, diepenv.SELF_DIM, diepenv.ENT_DIM, 0)
    tanks = hello[9]
    assert tanks >= 50 and len(hello) == 10 + tanks * 9
