#include "Entity/Misc/Boss/FallenSpike.h"
#include "Game.h"
#include "Entity/Tank/Addons.h"

#include <cmath>

FallenSpike::FallenSpike(GameServer* game) : AbstractBoss(game) {
    movementSpeed = 3.0;

    nameData.setName("Fallen Spike");

    damagePerTick *= 2;

    auto it = AddonById.find("spike");
    if (it != AddonById.end()) it->second(this);

    scale(std::pow(1.01, 75 - 1));
}

void FallenSpike::moveAroundMap() {
    if (ai->state == AIState::idle) {
        positionData.setAngle(positionData.angle + ai->passiveRotation);
        setVelocity(0, 0);
    } else {
        double x = positionData.x;
        double y = positionData.y;
        positionData.setAngle(std::atan2(ai->inputs->mouse.y - y, ai->inputs->mouse.x - x));
    }
}

void FallenSpike::tick(int tick) {
    AbstractBoss::tick(tick);
}
