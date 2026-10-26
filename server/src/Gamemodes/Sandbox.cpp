#include "Gamemodes/Sandbox.h"
#include "Client.h"
#include "Game.h"
#include "Const/Enums.h"

#include <cmath>

int SandboxShapeManager::wantedShapes() const {
    int i = 0;
    for (Client* client : arena->game->clients) {
        if (client->camera) i += 1;
    }
    return (int)std::floor(i * 12.5);
}

SandboxArena::SandboxArena(GameServer* game) : ArenaEntity(game) {
    delete shapes;
    shapes = new SandboxShapeManager(this);

    arenaData.setFlags(arenaData.flags | ArenaFlags::canUseCheats);
    state = ArenaState::OPEN;

    setSandboxArenaSize(0);
}

void SandboxArena::setSandboxArenaSize(int playerCount) {
    double arenaSize = std::floor(25.0 * std::sqrt(std::max(playerCount, 1))) * 100.0;
    updateBounds(arenaSize, arenaSize);
}

void SandboxArena::tick(int tick) {
    setSandboxArenaSize((int)game->clients.size());
    ArenaEntity::tick(tick);
}
