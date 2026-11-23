#pragma once

#include <cstdint>
#include <deque>
#include <string>
#include <unordered_map>
#include <vector>

#include "Duel.h"
#include "Match.h"

namespace live {

class LiveMatch;

// The bots' side of the brain socket. Frames both ways are u32 body length, u8 type, then the body, little-endian.
namespace frame {
constexpr uint8_t Hello = 1, Obs = 2, Act = 3;
constexpr uint32_t Version = 1;
}

// Bot flags in Hello.
namespace botflag {
constexpr uint8_t Remembers = 1;
}

// Row flags in Obs.
namespace rowflag {
constexpr uint8_t Deterministic = 1, First = 2;
}

class BrainBots : public Bots {
public:
    explicit BrainBots(const std::string& socketPath);
    ~BrainBots() override;

    void poll() override;
    void flush() override;
    bool online() const override;
    const Lists& lists() const override { return botLists; }
    std::unique_ptr<Match> start(const Spec& spec) override;

    // Blocks until a brain has said hello, or ms pass; true if one has.
    bool waitForBrain(int ms);
    // Sends what's queued and blocks until the brain has answered everything sent; false if it went away.
    bool sync();

    // A bot row of this tick's observations; returns the seq its frame will have.
    uint32_t queue(uint32_t match, int slot, int bot, int tank, uint8_t flags, const float* self, const float* ents,
                   const uint8_t* mask, const uint8_t* targetable, int rows);
    void forget(uint32_t match);
    uint32_t addMatch(LiveMatch* match);

private:
    struct Row {
        uint32_t match;
        uint8_t slot, bot, tank, flags;
        int rows;
        std::vector<float> self, ents;
        std::vector<uint8_t> mask, targetable;
    };
    struct Sent {
        uint32_t seq;
        std::vector<std::pair<uint32_t, uint8_t>> rows;
    };

    std::string path;
    int listenFd = -1, fd = -1;
    bool hello = false;
    Lists botLists;
    std::vector<uint8_t> in, out;
    uint32_t seq = 0;
    std::vector<Row> pending;
    std::deque<Sent> sent;
    // Ticks the oldest unanswered frame has waited.
    int waited = 0;
    uint32_t nextMatch = 1;
    std::unordered_map<uint32_t, LiveMatch*> matches;

    void accept();
    void drop();
    bool readSome();
    bool writeSome();
    void handle(uint8_t type, const uint8_t* body, size_t len);
    void onHello(const uint8_t* body, size_t len);
    void onAct(const uint8_t* body, size_t len);
};

class LiveMatch : public Match {
public:
    LiveMatch(BrainBots& brain, const Spec& spec, const bots::Config& config);
    ~LiveMatch() override;

    GameServer* game() override { return duel.game; }
    bool tick() override;
    void coast() override;
    bool alive(int seat) const override { return duel.isAlive(seat); }
    int ticksLeft() const override { return -1; }
    Result result() const override { return res; }

    // The brain's answer for a bot, for the observations sent in frame seq.
    void deliver(int slot, const bots::Action& action, uint32_t seq);
    bots::Duel& raw() { return duel; }

private:
    BrainBots& brain;
    uint32_t id;
    Spec spec;
    bots::Duel duel;
    bots::Action held[bots::MAX_PLAYERS];
    bool fresh[bots::MAX_PLAYERS] = {};
    uint32_t awaiting = 0;
    // Ticks into the decision being run.
    int phase = 0;
    bool first = true, ended = false;
    Result res;

    void observeBots();
};

}
