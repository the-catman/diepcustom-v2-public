#include "DuelShapes.h"

#include <cmath>

#include "Entity/Object.h"
#include "Entity/Shape/Pentagon.h"
#include "Entity/Shape/Square.h"
#include "Entity/Shape/Triangle.h"
#include "Game.h"
#include "Native/Arena.h"
#include "Native/Manager.h"
#include "Physics/CollisionManager.h"
#include "Util.h"

namespace bots {

DuelShapes::DuelShapes(ArenaEntity* arena, const ShapeMix& mix) : ShapeManager(arena), mix(mix) {}

AbstractShape* DuelShapes::pick() {
    double r = util::randomFloat();
    if (r < mix.pentagons) return Pentagon::makeRandom(game, false);
    if (r < mix.pentagons + (1 - mix.pentagons) / 6) return Triangle::makeRandom(game);
    return Square::makeRandom(game);
}

void DuelShapes::put(AbstractShape* shape, double x, double y) {
    shape->positionData.x = x;
    shape->positionData.y = y;
    shape->relationsData.owner = arena;
    shape->relationsData.team = arena;
    shape->scoreReward *= arena->shapeScoreRewardMultiplier;
    game->entities->collisionManager->insert(shape);
}

AbstractShape* DuelShapes::spawnShape() {
    AbstractShape* shape = pick();
    VectorAbstract pos = arena->findSpawnLocation();
    put(shape, pos.x, pos.y);
    return shape;
}

void DuelShapes::prefill(const std::vector<ObjectEntity*>& clearOf, double margin) {
    const ArenaData& ad = arena->arenaData;
    if ((int)shapes.size() < mix.count) shapes.resize(mix.count, nullptr);
    for (int i = 0; i < mix.count; ++i) {
        AbstractShape* shape = pick();
        double size = shape->physicsData.size;
        double x = 0, y = 0;
        for (int attempt = 0; attempt < 50; ++attempt) {
            x = ad.leftX + util::randomFloat() * (ad.rightX - ad.leftX);
            y = ad.topY + util::randomFloat() * (ad.bottomY - ad.topY);
            bool clear = true;
            for (ObjectEntity* o : clearOf) {
                double gap = o->physicsData.size + size + margin;
                double dx = o->positionData.x - x, dy = o->positionData.y - y;
                if (dx * dx + dy * dy < gap * gap) {
                    clear = false;
                    break;
                }
            }
            if (clear) break;
        }
        put(shape, x, y);
        shapes[i] = shape;
    }
}

AbstractShape* DuelShapes::place(int kind, double x, double y) {
    AbstractShape* shape = kind == 3   ? (AbstractShape*)new Triangle(game)
                           : kind == 4 ? (AbstractShape*)new Square(game)
                                       : (AbstractShape*)new Pentagon(game, kind == 6);
    put(shape, x, y);
    return shape;
}

}
