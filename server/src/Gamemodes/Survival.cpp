#include "Gamemodes/Survival.h"
#include "Native/Camera.h"
#include "Native/Entity.h"
#include "Client.h"
#include "Game.h"
#include "Config.h"
#include "Util.h"
#include "Const/Enums.h"

#include <cmath>

static constexpr int MIN_PLAYERS = 4;
static constexpr double SCORE_PER_TICK = 0.2;

int SurvivalShapeManager::wantedShapes() const {
    double ratio = std::ceil(std::pow(arena->width / 2500.0, 2));
    return (int)std::floor(12.5 * ratio);
}

SurvivalArena::SurvivalArena(GameServer* game) : ArenaEntity(game) {
    delete shapes;
    shapes = new SurvivalShapeManager(this);

    shapeScoreRewardMultiplier = 3.0;

    arenaData.setFlags(arenaData.flags & ~ArenaFlags::gameReadyStart);
    arenaData.setPlayersNeeded(MIN_PLAYERS);

    setSurvivalArenaSize(0);
}

void SurvivalArena::setSurvivalArenaSize(int playerCount) {
    double arenaSize = std::floor(25.0 * std::sqrt(std::max(playerCount, 1))) * 100.0;
    updateBounds(arenaSize, arenaSize);
}

void SurvivalArena::updateArenaState() {
    std::vector<TankBody*> players = getAlivePlayers();
    int aliveCount = (int)players.size();

    setSurvivalArenaSize(aliveCount);

    if (((int)(game->tick % (int)config::scoreboardUpdateInterval)) == 0) {
        updateScoreboard(players);
    }

    if (aliveCount <= 1 && state == ArenaState::OPEN) {
        state = ArenaState::OVER;
        close();
    }

    if (aliveCount == 0 && state == ArenaState::CLOSING) {
        state = ArenaState::CLOSED;
        GameServer* g = game;
        game->scheduleTimeout(5000, [g]() { g->end(); });
    }
}

void SurvivalArena::manageCountdown() {
    if (state == ArenaState::COUNTDOWN) {
        int needed = MIN_PLAYERS - (int)game->clientsAwaitingSpawn.size();
        arenaData.setPlayersNeeded(needed);
        if (needed <= 0) {
            arenaData.setFlags(arenaData.flags | ArenaFlags::gameReadyStart);
        } else {
            arenaData.setTicksUntilStart(config::countdownDuration);
            if (arenaData.flags & ArenaFlags::gameReadyStart)
                arenaData.setFlags(arenaData.flags & ~ArenaFlags::gameReadyStart);
        }
    }
    ArenaEntity::manageCountdown();
}

void SurvivalArena::onGameStarted() {
    ArenaEntity::onGameStarted();
    setSurvivalArenaSize((int)game->clientsAwaitingSpawn.size());
    arenaData.setFlags(arenaData.flags | ArenaFlags::noJoining);
}

void SurvivalArena::tick(int tick) {
    for (Client* client : game->clients) {
        ClientCamera* camera = client->camera;
        if (camera && Entity::exists(camera->cameraData.player)) {
            camera->addScore(SCORE_PER_TICK);
        }
    }
    ArenaEntity::tick(tick);
}
