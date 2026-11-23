#pragma once

#include <memory>
#include <string>
#include <vector>

class Client;
class GameServer;
class TankBody;

namespace bots {

// A camera that watches a recorded duel.
struct ReplayView {
    // The slot whose tank it follows with that tank's own view, or -1 for a fixed camera over the
    // arena's centre with this FOV.
    int follow = -1;
    double fov = 0.25;
};

// A message the server sent one view's client in the game-th game recorded.
struct Packet {
    int game = 0;
    int view = 0;
    int tick = 0;
    std::string bytes;
};

// Server clients whose messages are kept instead of sent. Each one's player is its own camera on
// the first tank's team, so the client draws that side as its own without steering a tank.
class Recorder {
public:
    std::vector<ReplayView> views;
    // The tanks' names, by slot.
    std::vector<std::string> names;
    std::vector<Packet> packets;

    Recorder();
    ~Recorder();
    void attach(GameServer* game, const std::vector<TankBody*>& tanks);
    // Follow cameras move to their tanks; call before each tick.
    void beforeTick(const std::vector<TankBody*>& tanks);
    // Drops what points into the game; call before deleting it, then finish() after.
    void release();
    void finish();

private:
    int games = -1;
    GameServer* game = nullptr;
    std::vector<std::unique_ptr<Client>> clients;
};

}
