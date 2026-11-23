#pragma once

#include <array>
#include <memory>
#include <utility>
#include <vector>

#include "Duel.h"

namespace bots {

// Many duels stepped together. Each external tank is a row.
class Vec {
public:
    Vec(const Config& config, int duels, int threads);
    Vec(const std::vector<Config>& configs, int threads);

    int rows() const { return (int)rowMap.size(); }
    // Takes effect at each duel's next reset; resetAll applies it now.
    void setConfig(const Config& config);
    void setDuelConfig(int duel, const Config& config);
    void resetAll();
    void seed(uint64_t seed);
    void setFrame(int duel, int slot, int frame) { duels.at(duel)->setFrame(slot, frame); }

    void step(const Action* actions, Outcome* outcomes, std::vector<Credit>& credits);
    // Fills self for every row and returns the entity width needed: the most rows any tank got
    // (at least 1). copyEntities then writes them that wide, zero-padded.
    int observe(float* self);
    void copyEntities(int width, float* ents, uint8_t* mask, uint8_t* targetable) const;
    // How many entities each row had on screen at the last observe, before entityCap.
    std::vector<int> seen;
    // The class each row's tank plays this game, and its opponent's in a two-tank game (-1 in bigger ones).
    std::vector<int> tankOf, enemyOf;
    // Whether each row's tank was alive at the last observe; a dead tank's game can go on without it.
    std::vector<uint8_t> aliveOf;
    // Duels that sit out step and observe: no ticks and no outcomes, and their rows all zeros.
    std::vector<uint8_t> paused;

    std::vector<std::unique_ptr<Duel>> duels;
    // (duel, slot) of each row.
    std::vector<std::pair<int, int>> rowMap;
    int threads;

private:
    std::vector<std::vector<Credit>> duelCredits;
    std::vector<std::array<Outcome, MAX_PLAYERS>> duelOutcomes;
    std::vector<float> entScratch;
    std::vector<uint8_t> maskScratch, targetScratch;
    std::vector<int> counts;
    void buildRows();
};

}
