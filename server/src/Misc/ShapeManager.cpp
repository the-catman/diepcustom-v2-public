#include "Misc/ShapeManager.h"
#include "Native/Arena.h"
#include "Native/Manager.h"
#include "Physics/CollisionManager.h"
#include "Entity/Shape/AbstractShape.h"
#include "Entity/Shape/Square.h"
#include "Entity/Shape/Triangle.h"
#include "Entity/Shape/Pentagon.h"
#include "Entity/Shape/Crasher.h"
#include "Game.h"
#include "Util.h"
#include "Config.h"
#include "Const/Enums.h"

#include <cmath>

ShapeManager::ShapeManager(ArenaEntity* arena) : arena(arena), game(arena->game) {
    shapes.resize(wantedShapes(), nullptr);
}

void ShapeManager::killAll() {
    for (AbstractShape* shape : shapes) {
        if (shape) shape->delete_();
    }
}

AbstractShape* ShapeManager::spawnShape() {
    VectorAbstract pos = arena->findSpawnLocation();
    double x = pos.x;
    double y = pos.y;
    double rightX = arena->arenaData.rightX;
    double leftX  = arena->arenaData.leftX;

    AbstractShape* shape;
    if (std::max(x, y) < rightX / 10 && std::min(x, y) > leftX / 10) {
        shape = Pentagon::makeRandom(game, util::randomFloat() <= 0.05);
    } else if (std::max(x, y) < rightX / 5 && std::min(x, y) > leftX / 5) {
        shape = new Crasher(game, util::randomFloat() < 0.2);
    } else {
        double r = util::randomFloat();
        if (r < 0.04)       shape = Pentagon::makeRandom(game, false);
        else if (r < 0.20)  shape = Triangle::makeRandom(game);
        else                shape = Square::makeRandom(game);
    }

    shape->positionData.x = x;
    shape->positionData.y = y;
    shape->relationsData.owner = arena;
    shape->relationsData.team  = arena;
    shape->scoreReward *= arena->shapeScoreRewardMultiplier;
    game->entities->collisionManager->insert(shape);
    return shape;
}

void ShapeManager::tick() {
    int wanted = wantedShapes();
    if ((int)shapes.size() < wanted) shapes.resize(wanted, nullptr);

    int spawned = 0;
    for (int i = wanted - 1; i >= 0; --i) {
        AbstractShape* s = shapes[i];
        // tag check frees slots of squares that were necro-claimed (identity taken by a drone)
        if (!s || s->hash == 0 || !(s->entityTags & EntityTags::isShape)) {
            shapes[i] = spawnShape();
            if (++spawned >= maxSpawnsPerTick()) break;
        }
    }
}
