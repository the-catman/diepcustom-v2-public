import numpy as np
import torch

from common import diepenv, make_config, tank_id
from model import Policy, _block, sample, log_prob, lead_log_prob, normal_lp, entropy, env_action

OBS_KEYS = ("self", "ents", "mask", "targetable")


def tensors(obs):
    return {k: torch.from_numpy(obs[k]) for k in OBS_KEYS}


def test_actions_step_the_env_and_log_probs_are_finite():
    torch.manual_seed(0)
    v = diepenv.Vec(make_config("fighter"), 8, 2)
    policy = Policy(d=32, heads=2, layers=1)
    obs = v.observe()
    for _ in range(30):
        out, _ = policy(tensors(obs))
        a = sample(out)
        lp = log_prob(out, a)
        assert torch.isfinite(lp).all()
        assert torch.isfinite(entropy(out)).all()
        # An untrained policy can only target real enemies or aim freely.
        chosen = a["target"] - 1
        targeted = chosen >= 0
        assert obs["targetable"][np.flatnonzero(targeted.numpy()), chosen[targeted].numpy()].all()
        e = {k: x.numpy() for k, x in env_action(a).items()}
        obs = v.step(e["move"], e["fire"], e["target"], e["aim"])


def test_the_attention_layers_work_as_torchs_own():
    torch.manual_seed(0)
    policy = Policy(d=32, heads=4, layers=2)
    x = torch.randn(6, 9, 32, requires_grad=True)
    mask = torch.rand(6, 9) < 0.6
    mask[:, 0] = True
    for block in policy.blocks:
        want = block(x, src_key_padding_mask=~mask)
        got = _block(block, x, mask[:, None, None, :])
        assert torch.allclose(got, want, atol=1e-5)
        gw = torch.autograd.grad(want.sum(), [x, *block.parameters()])
        gg = torch.autograd.grad(got.sum(), [x, *block.parameters()])
        assert all(torch.allclose(a, b, atol=1e-5) for a, b in zip(gw, gg))


def test_the_aim_heads_only_look_at_enemy_tanks():
    torch.manual_seed(0)
    policy = Policy(d=32, heads=2, layers=1, drones=True)
    for layer in (policy.lead[-1], policy.reach[-1]):
        torch.nn.init.normal_(layer.weight)
    c = make_config("fighter", arena_size=4000, spawn_min=600, spawn_max=1000, shapes_min=1.0, shapes_max=1.0)
    obs = tensors(diepenv.Vec(c, 6, 1).observe())
    out, _ = policy(obs)
    t = obs["targetable"]
    assert t.any() and (~t & obs["mask"]).any()
    assert (out["lead"][~t] == 0).all() and (out["reach"][~t] == 0).all() and (out["lead"][t] != 0).all()
    h, ents = policy.encode(obs)
    pair = torch.cat([h[:, None].expand_as(ents), ents], -1)
    assert torch.allclose(out["lead"][t], (1.2 * torch.tanh(policy.lead(pair).squeeze(-1)))[t], atol=1e-6)
    pointer = (policy.key(ents) @ policy.query(h)[:, :, None]).squeeze(-1) / 32 ** 0.5
    assert torch.allclose(out["target"][:, 1:][t], pointer[t], atol=1e-5)
    assert (out["target"][:, 1:][~t] < -1e8).all()


def test_untrained_lead_is_zero():
    policy = Policy(d=32, heads=2, layers=1)
    v = diepenv.Vec(make_config("still"), 4, 1)
    out, _ = policy(tensors(v.observe()))
    assert torch.allclose(out["lead"], torch.zeros_like(out["lead"]))


def test_chunk_replay_matches_step_by_step():
    torch.manual_seed(1)
    policy = Policy(d=32, heads=2, layers=1, memory=16)
    v = diepenv.Vec(make_config("fighter", max_decisions=5), 3, 1)
    obs = v.observe()
    state = policy.initial_state(3, "cpu")
    start = state.clone()
    seen, values, dones = [], [], []
    with torch.no_grad():
        for _ in range(8):
            o = tensors(obs)
            out, state = policy(o, state)
            seen.append(o)
            values.append(out["value"])
            a = env_action(sample(out))
            obs = v.step(*(a[k].numpy() for k in ("move", "fire", "target", "aim")))
            done = torch.from_numpy(obs["done"])
            state = state * (~done).float()[:, None]
            dones.append(done)
        assert torch.stack(dones).any(), "max_decisions=5 should end an episode inside the chunk"
        # Each step's entity arrays are as wide as that step needed.
        width = max(s["ents"].shape[1] for s in seen)
        pad = lambda t: torch.nn.functional.pad(t, (0, 0, 0, width - t.shape[1]) if t.dim() == 3 else (0, width - t.shape[1]))
        flat = {k: torch.cat([pad(s[k]) if k != "self" else s[k] for s in seen]) for k in OBS_KEYS}
        replay = policy.forward_seq(flat, start, torch.stack(dones), 8)
    assert torch.allclose(replay["value"], torch.cat(values), atol=1e-5)


def test_the_lead_angle_part_of_the_log_prob():
    torch.manual_seed(0)
    policy = Policy(d=32, heads=2, layers=1, self_dim=diepenv.SELF_DIM, ent_dim=diepenv.ENT_DIM, classes=40,
                    any_class=True)
    c = make_config("self", arena_size=4000, spawn_min=600, spawn_max=1000)
    c.tanks = [tank_id("fighter")] * 2
    v = diepenv.Vec(c, 4, 1)
    v.seed(2)
    v.reset_all()
    o = v.observe()
    obs = {k: torch.from_numpy(o[k]) for k in ("self", "ents", "mask", "targetable", "tank")}
    out, _ = policy(obs)
    n = len(obs["self"])
    act = {"move": torch.zeros(n, dtype=torch.long), "fire": torch.zeros(n, dtype=torch.bool),
           "target": torch.tensor([0, 1] * (n // 2)), "aim": torch.full((n,), 0.1),
           "repel": torch.zeros(n, dtype=torch.bool), "reach": torch.full((n,), 300.0)}
    lead = lead_log_prob(out, act)
    assert (lead[act["target"] == 0] == 0).all()
    want = normal_lp(act["aim"], out["lead"][:, 0], out["lead_logstd"])
    assert torch.allclose(lead[act["target"] == 1], want[act["target"] == 1])
    # The rest of the log prob doesn't move with the lead angle.
    shifted = dict(out, lead=out["lead"] + 0.05)
    assert torch.allclose(log_prob(out, act) - lead, log_prob(shifted, act) - lead_log_prob(shifted, act), atol=1e-5)
