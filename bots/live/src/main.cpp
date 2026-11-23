#include <cstdlib>
#include <memory>
#include <string>

#include "Host.h"
#include "Match.h"
#include "Rooms.h"

// Run from server/, so data/ and ../client/dist resolve.
int main() {
    host::init();
    const char* socket = std::getenv("BOTS_SOCKET");
    std::unique_ptr<live::Bots> bots = live::connectBrain(socket && *socket ? socket : "../bots/runs/live.sock");
    live::Rooms rooms(*bots);
    rooms.open(host::addMode("bots", "Bots", rooms.lobbyArena()).shared);
    host::onTick([&rooms]() { rooms.tick(); });
    host::onClose([&rooms](Client* client) { rooms.onClose(client); });
    return host::run();
}
