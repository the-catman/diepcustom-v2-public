#include "Vec.h"

#include <algorithm>
#include <array>
#include <cstring>

#include <omp.h>

#include "Util.h"

namespace bots {

Vec::Vec(const Config& config, int duels, int threads)
    : Vec(std::vector<Config>(std::max(0, duels), config), threads) {}

Vec::Vec(const std::vector<Config>& configs, int threads) : threads(std::max(1, threads)) {
    int n = (int)configs.size();
    this->duels.resize(n);
#pragma omp parallel for schedule(static) num_threads(this->threads)
    for (int i = 0; i < n; ++i) this->duels[i] = std::make_unique<Duel>(configs[i]);
    duelCredits.resize(n);
    duelOutcomes.resize(n);
    paused.assign(n, 0);
    buildRows();
}

void Vec::buildRows() {
    rowMap.clear();
    for (int d = 0; d < (int)duels.size(); ++d) {
        const Config& c = duels[d]->config;
        for (int s = 0; s < c.players; ++s) {
            if (c.kinds[s] == External || c.exposeScripted) rowMap.push_back({d, s});
        }
    }
}

void Vec::setConfig(const Config& config) {
    for (auto& d : duels) d->config = config;
    buildRows();
}

void Vec::setDuelConfig(int duel, const Config& config) {
    duels.at(duel)->config = config;
    buildRows();
}

void Vec::resetAll() {
    int n = (int)duels.size();
#pragma omp parallel for schedule(static) num_threads(threads)
    for (int d = 0; d < n; ++d) duels[d]->reset();
}

void Vec::seed(uint64_t seed) {
#pragma omp parallel num_threads(threads)
    util::seedRandom(seed * 1000003ull + (uint64_t)omp_get_thread_num());
}

void Vec::step(const Action* actions, Outcome* outcomes, std::vector<Credit>& credits) {
    int n = (int)duels.size();
    std::vector<std::array<Action, MAX_PLAYERS>> perDuel(n);
    for (int r = 0; r < rows(); ++r) perDuel[rowMap[r].first][rowMap[r].second] = actions[r];

#pragma omp parallel for schedule(static) num_threads(threads)
    for (int d = 0; d < n; ++d) {
        duelCredits[d].clear();
        if (paused[d]) {
            duelOutcomes[d] = {};
            continue;
        }
        duels[d]->step(perDuel[d].data(), duelOutcomes[d].data(), duelCredits[d]);
    }

    std::vector<int> rowOf(n * MAX_PLAYERS, -1);
    for (int r = 0; r < rows(); ++r) {
        rowOf[rowMap[r].first * MAX_PLAYERS + rowMap[r].second] = r;
        outcomes[r] = duelOutcomes[rowMap[r].first][rowMap[r].second];
    }
    credits.clear();
    for (int d = 0; d < n; ++d) {
        for (const Credit& c : duelCredits[d]) {
            int r = rowOf[d * MAX_PLAYERS + c.slot];
            if (r >= 0) credits.push_back({r, c.stepsAgo, c.value});
        }
    }
}

int Vec::observe(float* self) {
    int n = rows();
    entScratch.resize((size_t)n * ENT_MAX * ENT_DIM);
    maskScratch.resize((size_t)n * ENT_MAX);
    targetScratch.resize((size_t)n * ENT_MAX);
    counts.assign(n, 0);
    seen.assign(n, 0);
    tankOf.assign(n, 0);
    enemyOf.assign(n, 0);
    aliveOf.assign(n, 0);
#pragma omp parallel for schedule(static) num_threads(threads)
    for (int r = 0; r < n; ++r) {
        auto [d, s] = rowMap[r];
        if (paused[d]) {
            std::fill_n(self + (size_t)r * SELF_DIM, SELF_DIM, 0.0f);
            continue;
        }
        counts[r] = duels[d]->observe(s, self + (size_t)r * SELF_DIM, &entScratch[(size_t)r * ENT_MAX * ENT_DIM],
                                      &maskScratch[(size_t)r * ENT_MAX], &targetScratch[(size_t)r * ENT_MAX], &seen[r]);
        const Duel& duel = *duels[d];
        tankOf[r] = duel.slots[s].tankId;
        enemyOf[r] = duel.config.players == 2 ? duel.slots[1 - s].tankId : -1;
        aliveOf[r] = duel.isAlive(s);
    }
    int width = 1;
    for (int c : counts) width = std::max(width, c);
    return width;
}

void Vec::copyEntities(int width, float* ents, uint8_t* mask, uint8_t* targetable) const {
    int n = rows();
#pragma omp parallel for schedule(static) num_threads(threads)
    for (int r = 0; r < n; ++r) {
        size_t c = counts[r], w = width;
        float* e = ents + (size_t)r * w * ENT_DIM;
        std::memcpy(e, &entScratch[(size_t)r * ENT_MAX * ENT_DIM], c * ENT_DIM * sizeof(float));
        std::memset(e + c * ENT_DIM, 0, (w - c) * ENT_DIM * sizeof(float));
        std::memcpy(mask + r * w, &maskScratch[(size_t)r * ENT_MAX], c);
        std::memset(mask + r * w + c, 0, w - c);
        std::memcpy(targetable + r * w, &targetScratch[(size_t)r * ENT_MAX], c);
        std::memset(targetable + r * w + c, 0, w - c);
    }
}

}
