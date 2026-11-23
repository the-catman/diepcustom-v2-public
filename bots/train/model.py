import inspect
import math

import torch
import torch.nn as nn
import torch.nn.functional as F

from common import MOVES, OLD_ENT_DIM, OLD_SELF_DIM

NEG = -1e9
# Largest lead the policy can put on a target, in radians.
LEAD_RANGE = 1.2
# The range the lead angle's log spread is held to.
LEAD_LOGSTD = (-4.0, -0.5)
# Drone classes also place the mouse. On a target, its distance is the target's times
# exp(reach) with reach within this, about 0.14x to 7x; free, exp(reach) itself within FREE_REACH.
REACH_RANGE = 2.0
FREE_REACH = (math.log(40), math.log(3000))
# How much of the self token the first bots were shown. The rest, where the tank is on the map and
# where it last saw the enemy, came with the big map.
BASE_SELF_DIM = 32
# The self feature that says the tank's drones follow its mouse.
MOUSE_FEATURE = OLD_SELF_DIM
# Grouping rows by screen size in a forward pass: at most GROUPS groups, cut at multiples of 1/GROUP_STEPS of
# the rows, each group costing about GROUP_COST padded tokens on top of its work. Attention's share of the work
# per token grows with the width over ATTENTION_WIDTH. Batches smaller or narrower than the minimums run whole.
GROUPS = 3
GROUP_STEPS = 16
GROUP_COST = 4000
ATTENTION_WIDTH = 512
GROUP_MIN_ROWS = 32
GROUP_MIN_WIDTH = 24


def wrap(x):
    return torch.atan2(torch.sin(x), torch.cos(x))


def normal_lp(x, mean, logstd):
    return -0.5 * ((x - mean) / logstd.exp()) ** 2 - logstd - 0.5 * math.log(2 * math.pi)


def trim(obs):
    """Drops entity slots no row in the batch uses; visible entities fill the slots from the front."""
    used = obs["mask"].any(0)
    n = max(int(used.sum()), 1)
    if n == used.shape[0]:
        return obs
    return {k: v[:, :n] if k in ("ents", "mask", "targetable") else v for k, v in obs.items()}


def model_shape(model_args):
    """model_args with the defaults filled in, so checkpoints from before a new argument compare equal."""
    params = inspect.signature(Policy.__init__).parameters
    return {k: model_args.get(k, p.default) for k, p in params.items() if k != "self"}


def widen(state, self_dim, ent_dim=OLD_ENT_DIM):
    """Weights for a policy shown more of the self or entity tokens; it starts out ignoring the new inputs."""
    state = dict(state)
    for key, dim in (("self_enc.0.weight", self_dim), ("ent_enc.0.weight", ent_dim)):
        w = state[key]
        if w.shape[1] < dim:
            state[key] = torch.cat([w, w.new_zeros(w.shape[0], dim - w.shape[1])], 1)
    return state


def adapt(state, policy):
    """Weights saved by a smaller policy, fitted to this one. New inputs start out ignored, and new parts (the
    class embedding, drone controls) start as a fresh policy has them, so it plays as the saved one did."""
    state = widen(state, policy.self_dim, policy.ent_dim)
    for k, v in policy.state_dict().items():
        state.setdefault(k, v)
    return state


class Policy(nn.Module):
    """Self token plus one token per visible entity, mixed by a few attention layers.

    Aiming picks a target with a pointer over the entity tokens (or "free", an absolute angle),
    then a lead angle added to the target's direction. A zero lead means aiming straight at it.
    With drones, it also right clicks to repel, and places the mouse: by default on the target.
    """

    def __init__(self, d=128, heads=4, layers=2, memory=0, drones=False, self_dim=BASE_SELF_DIM, ent_dim=OLD_ENT_DIM,
                 classes=0, any_class=False):
        super().__init__()
        self.d = d
        self.self_dim = self_dim
        self.ent_dim = ent_dim
        self.memory = memory
        # One policy for every class: the drone controls are always there, and only drone tanks use them.
        self.any_class = any_class
        self.drones = drones or any_class
        drones = self.drones
        if memory:
            self.gru = nn.GRUCell(d, memory)
            self.mem_out = nn.Linear(memory, d)
        self.self_enc = nn.Sequential(nn.Linear(self_dim, d), nn.ReLU(), nn.Linear(d, d))
        self.ent_enc = nn.Sequential(nn.Linear(ent_dim, d), nn.ReLU(), nn.Linear(d, d))
        # Added to the self token, by the class it plays. Starts at zero, so the class features carry it at first.
        self.classes = classes
        if classes:
            self.cls = nn.Embedding(classes, d)
            nn.init.zeros_(self.cls.weight)
        self.blocks = nn.ModuleList(
            nn.TransformerEncoderLayer(d, heads, 2 * d, dropout=0.0, batch_first=True, norm_first=True)
            for _ in range(layers))
        self.norm = nn.LayerNorm(d)
        self.move = nn.Linear(d, MOVES)
        self.fire = nn.Linear(d, 1)
        self.value = nn.Sequential(nn.Linear(d, d), nn.ReLU(), nn.Linear(d, 1))
        self.query = nn.Linear(d, d)
        self.key = nn.Linear(d, d)
        self.free = nn.Linear(d, 1)
        self.lead = nn.Sequential(nn.Linear(2 * d, d), nn.ReLU(), nn.Linear(d, 1))
        self.free_aim = nn.Linear(d, 2)
        self.lead_logstd = nn.Parameter(torch.tensor(math.log(0.15)))
        self.free_logstd = nn.Parameter(torch.tensor(math.log(0.5)))
        nn.init.zeros_(self.lead[-1].weight)
        nn.init.zeros_(self.lead[-1].bias)
        if drones:
            self.repel = nn.Linear(d, 1)
            nn.init.constant_(self.repel.bias, -2.0)
            self.reach = nn.Sequential(nn.Linear(2 * d, d), nn.ReLU(), nn.Linear(d, 1))
            self.free_reach = nn.Linear(d, 1)
            for layer in (self.reach[-1], self.free_reach):
                nn.init.zeros_(layer.weight)
                nn.init.zeros_(layer.bias)
            self.reach_logstd = nn.Parameter(torch.tensor(math.log(0.1)))
            self.free_reach_logstd = nn.Parameter(torch.tensor(math.log(0.3)))

    def initial_state(self, rows, device):
        return torch.zeros(rows, self.memory, device=device) if self.memory else None

    def encode(self, obs):
        s, e, mask = obs["self"][:, :self.self_dim], obs["ents"][..., :self.ent_dim], obs["mask"]
        b = s.shape[0]
        me = self.self_enc(s)
        if self.classes:
            me = me + self.cls(obs["tank"].long().clamp(0, self.classes - 1))
        x = torch.cat([me[:, None], self.ent_enc(e)], 1)
        seen = torch.cat([torch.ones(b, 1, dtype=torch.bool, device=s.device), mask], 1)[:, None, None, :]
        for block in self.blocks:
            x = _block(block, x, seen)
        x = self.norm(x)
        return x[:, 0], x[:, 1:]

    def remember(self, h, state):
        state = self.gru(h, state)
        return h + self.mem_out(state), state

    def heads(self, h, ents, obs):
        """The per-entity heads only run on the entities it can target; the rest keep a lead and reach of 0."""
        b, n, _ = ents.shape
        rows, cols = obs["targetable"].nonzero(as_tuple=True)
        mine, theirs = h[rows], ents[rows, cols]
        at = (rows, cols)
        pointer = (self.key(theirs) * self.query(mine)).sum(-1) / math.sqrt(self.d)
        pointer = ents.new_full((b, n), NEG).index_put(at, pointer)
        target_logits = torch.cat([self.free(h), pointer], 1)
        pair = torch.cat([mine, theirs], -1)
        zero = ents.new_zeros(b, n)
        lead = zero.index_put(at, LEAD_RANGE * torch.tanh(self.lead(pair).squeeze(-1)))
        fa = self.free_aim(h)
        out = {
            "move": self.move(h),
            "fire": self.fire(h).squeeze(-1),
            "target": target_logits,
            "lead": lead,
            "free_aim": torch.atan2(fa[:, 1], fa[:, 0]),
            "lead_logstd": self.lead_logstd.clamp(*LEAD_LOGSTD),
            "free_logstd": self.free_logstd.clamp(-3.0, 0.5),
            "value": self.value(h).squeeze(-1),
        }
        if self.drones:
            lo, hi = FREE_REACH
            out.update({
                "repel": self.repel(h).squeeze(-1),
                "reach": zero.index_put(at, REACH_RANGE * torch.tanh(self.reach(pair).squeeze(-1))),
                "free_reach": (lo + hi) / 2 + (hi - lo) / 2 * torch.tanh(self.free_reach(h).squeeze(-1)),
                "reach_logstd": self.reach_logstd.clamp(-4.0, 0.0),
                "free_reach_logstd": self.free_reach_logstd.clamp(-3.0, 0.5),
            })
            if self.any_class:
                out["mouse"] = obs["self"][:, MOUSE_FEATURE] > 0.5
        return out

    def encode_grouped(self, obs):
        """encode, with rows grouped by how busy their screens are, so quiet screens aren't padded out to
        the busiest. Entity outputs past a row's own entities are zero rather than padding's."""
        b, n = obs["mask"].shape
        if b < GROUP_MIN_ROWS or n < GROUP_MIN_WIDTH:
            return self.encode(obs)
        counts = obs["mask"].sum(1)
        order = torch.argsort(counts)
        sizes = counts[order].tolist()
        cuts = _group_cuts(sizes)
        if len(cuts) == 1:
            return self.encode(obs)
        h = obs["self"].new_empty(b, self.d)
        ents = obs["self"].new_zeros(b, n, self.d)
        i = 0
        for j in cuts:
            rows, w = order[i:j], max(sizes[j - 1], 1)
            part = {"self": obs["self"][rows], "ents": obs["ents"][rows, :w], "mask": obs["mask"][rows, :w]}
            if self.classes:
                part["tank"] = obs["tank"][rows]
            h[rows], ents[rows, :w] = self.encode(part)
            i = j
        return h, ents

    def forward(self, obs, state=None):
        """One decision. Returns the heads and the new memory state (None without memory)."""
        obs = trim(obs)
        h, ents = self.encode_grouped(obs)
        if self.memory:
            h, state = self.remember(h, state)
        return self.heads(h, ents, obs), state

    def forward_seq(self, obs, state, done, steps):
        """steps decisions of len(state) sequences, flattened time-major in obs.

        done[t] marks an episode ending at decision t, which clears the memory before t + 1.
        """
        obs = trim(obs)
        h, ents = self.encode(obs)
        if self.memory:
            h = h.view(steps, -1, self.d)
            outs = []
            for t in range(steps):
                z, state = self.remember(h[t], state)
                outs.append(z)
                state = state * (~done[t]).float()[:, None]
            h = torch.stack(outs).view(-1, self.d)
        return self.heads(h, ents, obs)


def _group_cuts(sizes):
    """Ends of the row groups encode_grouped runs, for rows sorted by entity count: up to GROUPS groups,
    cut where the padded work plus a fixed cost per group is least."""
    b = len(sizes)

    def cost(i, j):
        w = sizes[j - 1] + 1
        return (j - i) * (w + w * w / ATTENTION_WIDTH) + GROUP_COST

    picks = sorted({b * k // GROUP_STEPS for k in range(1, GROUP_STEPS)} - {0})
    best, cuts = cost(0, b), [b]
    for x in picks:
        c = cost(0, x) + cost(x, b)
        if c < best:
            best, cuts = c, [x, b]
        if GROUPS > 2:
            for y in picks:
                if y > x:
                    c = cost(0, x) + cost(x, y) + cost(y, b)
                    if c < best:
                        best, cuts = c, [x, y, b]
    return cuts


def _block(block, x, seen):
    """A pre-norm nn.TransformerEncoderLayer with its own weights, without the copies its own forward makes."""
    b, n, d = x.shape
    attn = block.self_attn
    heads = attn.num_heads
    y = F.layer_norm(x, (d,), block.norm1.weight, block.norm1.bias, block.norm1.eps)
    qkv = F.linear(y, attn.in_proj_weight, attn.in_proj_bias)
    q, k, v = qkv.view(b, n, 3, heads, d // heads).permute(2, 0, 3, 1, 4)
    o = F.scaled_dot_product_attention(q, k, v, attn_mask=seen)
    x = x + attn.out_proj(o.transpose(1, 2).reshape(b, n, d))
    y = F.layer_norm(x, (d,), block.norm2.weight, block.norm2.bias, block.norm2.eps)
    return x + block.linear2(F.relu(block.linear1(y)))


def _at(per_entity, target):
    # target: 0 = free, i + 1 = entity row i.
    idx = (target - 1).clamp(min=0)
    return per_entity.gather(1, idx[:, None]).squeeze(1)


def _lead_of(out, target):
    return _at(out["lead"], target)


def sample(out, deterministic=False):
    if deterministic:
        move = out["move"].argmax(-1)
        fire = out["fire"] > 0
        target = out["target"].argmax(-1)
        lead = _lead_of(out, target)
        aim = torch.where(target > 0, lead, out["free_aim"]).detach()
    else:
        move = torch.distributions.Categorical(logits=out["move"], validate_args=False).sample()
        fire = torch.bernoulli(torch.sigmoid(out["fire"])).bool()
        target = torch.distributions.Categorical(logits=out["target"], validate_args=False).sample()
        lead = _lead_of(out, target) + torch.randn_like(out["free_aim"]) * out["lead_logstd"].exp()
        free = wrap(out["free_aim"] + torch.randn_like(out["free_aim"]) * out["free_logstd"].exp())
        aim = torch.where(target > 0, lead, free)
    act = {"move": move, "fire": fire, "target": target, "aim": aim.detach()}
    if "repel" in out:
        # Drawn after everything else, so the parts above match a model without drones.
        placed = _at(out["reach"], target)
        if deterministic:
            repel = out["repel"] > 0
            reach = torch.where(target > 0, placed, out["free_reach"])
        else:
            repel = torch.bernoulli(torch.sigmoid(out["repel"])).bool()
            placed = placed + torch.randn_like(placed) * out["reach_logstd"].exp()
            free = out["free_reach"] + torch.randn_like(placed) * out["free_reach_logstd"].exp()
            reach = torch.where(target > 0, placed, free)
        if "mouse" in out:
            # Tanks without mouse drones never right click. Their reach is kept finite here, and only becomes
            # "mouse far away" in env_action, so log_prob never touches a NaN.
            repel = repel & out["mouse"]
            act["mouse"] = out["mouse"]
        act["repel"], act["reach"] = repel, reach.detach()
    return act


def log_prob(out, act):
    lp = torch.log_softmax(out["move"], -1).gather(1, act["move"][:, None]).squeeze(1)
    lp = lp - F.binary_cross_entropy_with_logits(out["fire"], act["fire"].float(), reduction="none")
    lp = lp + torch.log_softmax(out["target"], -1).gather(1, act["target"][:, None]).squeeze(1)
    targeted = act["target"] > 0
    lead_lp = normal_lp(act["aim"], _lead_of(out, act["target"]), out["lead_logstd"])
    free_lp = normal_lp(wrap(act["aim"] - out["free_aim"]), 0, out["free_logstd"])
    lp = lp + torch.where(targeted, lead_lp, free_lp)
    if "repel" in out:
        repel_lp = -F.binary_cross_entropy_with_logits(out["repel"], act["repel"].float(), reduction="none")
        placed_lp = normal_lp(act["reach"], _at(out["reach"], act["target"]), out["reach_logstd"])
        free_reach_lp = normal_lp(act["reach"], out["free_reach"], out["free_reach_logstd"])
        # Without a click the drones fly themselves, so where the mouse was changed nothing.
        clicked = act["fire"] | act["repel"]
        reach_lp = torch.where(clicked, torch.where(targeted, placed_lp, free_reach_lp), torch.zeros_like(lp))
        if "mouse" in out:
            # A tank without mouse drones never chose whether to right click or where its mouse went.
            lp = lp + torch.where(out["mouse"], repel_lp + reach_lp, torch.zeros_like(lp))
        else:
            lp = lp + repel_lp + reach_lp
    return lp


def lead_log_prob(out, act):
    """The lead angle's part of log_prob: nothing where the policy aimed freely."""
    lead_lp = normal_lp(act["aim"], _lead_of(out, act["target"]), out["lead_logstd"])
    return torch.where(act["target"] > 0, lead_lp, torch.zeros_like(lead_lp))


def entropy(out):
    """Entropy of the discrete parts only; a bonus on the aim spread rewards spraying."""
    ent = torch.distributions.Categorical(logits=out["move"], validate_args=False).entropy()
    p = torch.sigmoid(out["fire"])
    ent = ent - (p * torch.log(p.clamp_min(1e-8)) + (1 - p) * torch.log((1 - p).clamp_min(1e-8)))
    ent = ent + torch.distributions.Categorical(logits=out["target"], validate_args=False).entropy()
    if "repel" in out:
        q = torch.sigmoid(out["repel"])
        repel_ent = -(q * torch.log(q.clamp_min(1e-8)) + (1 - q) * torch.log((1 - q).clamp_min(1e-8)))
        if "mouse" in out:
            repel_ent = repel_ent * out["mouse"]
        ent = ent + repel_ent
    return ent


def env_action(act):
    """Policy action -> what Vec.step takes."""
    out = {
        "move": act["move"].int(),
        "fire": act["fire"],
        "target": (act["target"] - 1).int(),
        "aim": act["aim"].float(),
    }
    if "repel" in act:
        out["repel"] = act["repel"]
        out["reach"] = act["reach"].float()
        if "mouse" in act:
            out["reach"] = torch.where(act["mouse"], out["reach"], torch.full_like(out["reach"], float("nan")))
    return out
