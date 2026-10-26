#include "Entity/Shape/Crasher.h"
#include "Game.h"
#include "Config.h"
#include "Const/Enums.h"

#include <cmath>

Crasher::Crasher(GameServer* game, bool large) : AbstractShape(game), isLarge(large) {
    nameData.setName("Crasher");

    positionData.flags |= PositionFlags::canMoveThroughWalls;
    healthData.health = healthData.maxHealth = large ? 30 : 10;
    physicsData.size  = (large ? 55 : 35) * M_SQRT1_2;
    physicsData.sides = 3;
    physicsData.absorbtionFactor = large ? 0.1 : 2;
    physicsData.pushFactor       = large ? 12  : 8;

    styleData.color = Color::EnemyCrasher;

    scoreReward   = large ? 25 : 15;
    damagePerTick = 2;
    targettingSpeed = large ? 2.64 : 2.602;

    ai = new AI(this);
    ai->viewRange = 2000;
    ai->movementSpeed = targettingSpeed;
    ai->aimSpeed = targettingSpeed * 9; // prediction wants travel speed, not accel; terminal v = 9a
    ai->doAimPrediction = true;
    ai->setFindTargetInterval((int)config::tps);

    arenaMobID = "crasher";
}

void Crasher::tick(int tick) {
    ai->aimSpeed = targettingSpeed * 9;
    ai->movementSpeed = targettingSpeed;

    if (ai->state == AIState::idle) {
        doIdleRotate = true;
    } else {
        doIdleRotate = false;
        positionData.setAngle(std::atan2(
            ai->inputs->mouse.y - positionData.y,
            ai->inputs->mouse.x - positionData.x
        ));
        velocity.add({
            ai->inputs->movement.x * targettingSpeed,
            ai->inputs->movement.y * targettingSpeed
        });
    }

    ai->inputs->movement.set({0, 0});

    AbstractShape::tick(tick);
}
