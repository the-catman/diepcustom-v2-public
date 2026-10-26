#include "Misc/BossManager.h"
#include "Entity/Boss/AbstractBoss.h"
#include "Entity/Boss/Guardian.h"
#include "Entity/Boss/Summoner.h"
#include "Entity/Boss/FallenOverlord.h"
#include "Entity/Boss/FallenBooster.h"
#include "Entity/Boss/Defender.h"
#include "Native/Arena.h"
#include "Native/Entity.h"
#include "Native/Manager.h"
#include "Physics/CollisionManager.h"
#include "Physics/Vector.h"
#include "Game.h"
#include "Config.h"
#include "Util.h"

#include <functional>
#include <vector>

using BossFactory = std::function<AbstractBoss*(GameServer*)>;

static const std::vector<BossFactory> bossClasses = {
    [](GameServer* g) { return (AbstractBoss*)new Guardian(g); },
    [](GameServer* g) { return (AbstractBoss*)new Summoner(g); },
    [](GameServer* g) { return (AbstractBoss*)new FallenOverlord(g); },
    [](GameServer* g) { return (AbstractBoss*)new FallenBooster(g); },
    [](GameServer* g) { return (AbstractBoss*)new Defender(g); },
};

BossManager::BossManager(ArenaEntity* arena) : arena(arena) {}

void BossManager::spawnBoss() {
    size_t idx = (size_t)(util::randomFloat() * bossClasses.size());
    boss = bossClasses[idx](arena->game);

    double w = arena->width / 2;
    double h = arena->height / 2;
    VectorAbstract pos = arena->findSpawnLocation(w, h);

    boss->positionData.x = pos.x;
    boss->positionData.y = pos.y;
    arena->game->entities->collisionManager->insert(boss);
}

void BossManager::tick(int tick) {
    if (boss && !Entity::exists(boss)) boss = nullptr;

    static const int interval = (int)config::bossSpawningInterval;
    if (tick >= 1 && (tick % interval) == 0 && !boss) {
        spawnBoss();
    }
}
