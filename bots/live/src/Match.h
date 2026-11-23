#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

class Client;
class GameServer;

namespace live {

// One seat of a match. bot -1 is a Player, whose client is moved into the match's game.
struct Seat {
    int bot = -1;
    int tank = 0;
    std::string name;
    Client* client = nullptr;
    // A Player's own build, 8 stats in server order; the tank's usual one if empty.
    std::optional<std::array<int, 8>> build;
};

// Team 0's n0 seats first, then team 1's n1.
struct Spec {
    int n0 = 1, n1 = 1;
    std::vector<Seat> seats;
    bool deterministic = false;
};

struct Result {
    // Team 0 or 1, or -1 for a draw.
    int winner = -1;
    bool timedOut = false;
    int ticks = 0;
    int kills[8] = {};
    float dealt[8] = {};
};

struct Match {
    virtual ~Match() = default;
    // Where the seated clients now are.
    virtual GameServer* game() = 0;
    // One game tick; true once it has ended.
    virtual bool tick() = 0;
    // One tick after the end, while the result is up.
    virtual void coast() = 0;
    virtual bool alive(int seat) const = 0;
    virtual int ticksLeft() const = 0;
    virtual Result result() const = 0;
};

// Each tank: its id, then 8 stats in server order.
struct Lists {
    struct Bot {
        std::string name;
        bool remembers = false;
        std::vector<std::array<int, 9>> tanks;
    };
    std::vector<Bot> bots;
    std::vector<std::array<int, 9>> playerTanks;
    // Changes whenever the lists do.
    uint32_t version = 0;
};

struct Bots {
    virtual ~Bots() = default;
    // Reads the brain's replies.
    virtual void poll() = 0;
    // Sends this tick's observations.
    virtual void flush() = 0;
    virtual bool online() const = 0;
    virtual const Lists& lists() const = 0;
    // Moves the Player seats' clients into a new game and spawns everyone.
    virtual std::unique_ptr<Match> start(const Spec&) = 0;
};

std::unique_ptr<Bots> connectBrain(const std::string& socketPath);

}
