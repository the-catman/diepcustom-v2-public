#include "Brain.h"

#include <algorithm>
#include <vector>

namespace live {

LiveMatch::LiveMatch(BrainBots& brain, const Spec& spec, const bots::Config& config)
    : brain(brain), spec(spec), duel(config, bots::Duel::Deferred{}) {
    id = brain.addMatch(this);
    int n = config.players;
    for (int i = 0; i < n; ++i) {
        const Seat& seat = spec.seats[i];
        if (seat.bot < 0) duel.seat(i, seat.client);
        duel.names[i] = seat.name;
    }
    duel.reset();
    observeBots();
}

LiveMatch::~LiveMatch() { brain.forget(id); }

void LiveMatch::observeBots() {
    static thread_local std::vector<float> self(bots::SELF_DIM), ents((size_t)bots::ENT_MAX * bots::ENT_DIM);
    static thread_local std::vector<uint8_t> mask(bots::ENT_MAX), targetable(bots::ENT_MAX);
    uint8_t flags = (spec.deterministic ? rowflag::Deterministic : 0) | (first ? rowflag::First : 0);
    for (int i = 0; i < duel.config.players; ++i) {
        const Seat& seat = spec.seats[i];
        if (seat.bot < 0 || !duel.isAlive(i)) continue;
        int seen = 0;
        int rows = duel.observe(i, self.data(), ents.data(), mask.data(), targetable.data(), &seen);
        awaiting = brain.queue(id, i, seat.bot, seat.tank, flags, self.data(), ents.data(), mask.data(),
                               targetable.data(), rows);
    }
    first = false;
}

void LiveMatch::deliver(int slot, const bots::Action& action, uint32_t seq) {
    if (seq != awaiting || ended) return;
    held[slot] = action;
    if (phase == 0) fresh[slot] = true;
    else duel.act(slot, action);
}

bool LiveMatch::tick() {
    if (ended) return true;
    if (phase == 0) {
        if (!brain.online()) {
            // Bots without a brain stand still and hold fire.
            for (int i = 0; i < duel.config.players; ++i) {
                held[i] = bots::Action{};
                fresh[i] = true;
            }
        }
        duel.begin(held, fresh);
        std::fill(std::begin(fresh), std::end(fresh), false);
    }
    bool over = duel.advance();
    phase += 1;
    res.ticks += 1;
    if (!over && phase < duel.config.repeat) return false;
    bots::Outcome out[bots::MAX_PLAYERS];
    std::vector<bots::Credit> credits;
    bool done = duel.settle(out, credits, false);
    phase = 0;
    for (int i = 0; i < duel.config.players; ++i) {
        res.kills[i] += out[i].kills;
        res.dealt[i] += out[i].dealt;
    }
    if (!done) {
        observeBots();
        return false;
    }
    ended = true;
    res.winner = -1;
    for (int i = 0; i < duel.config.players; ++i)
        if (out[i].teamWon) res.winner = i < spec.n0 ? 0 : 1;
    return true;
}

void LiveMatch::coast() { duel.coast(); }

}
