#include "Entity/Misc/Boss/FallenAC.h"
#include "Game.h"
#include "Entity/Tank/Barrel.h"
#include "Const/Enums.h"
#include "Const/TankDefinitions.h"

#include <cmath>

FallenAC::FallenAC(GameServer* game) : AbstractBoss(game) {
    nameData.setName("Fallen Arena Closer");
    movementSpeed = 20;

    TankDefinition* def = TankDefinitions[Tank::ArenaCloser];
    for (BarrelDefinition& bd : def->barrels) {
        barrels.push_back(new Barrel(this, &bd));
    }

    scale(std::pow(1.01, 75 - 1));
}

void FallenAC::moveAroundMap() {
    if (ai->state == AIState::idle) {
        positionData.setAngle(positionData.angle + ai->passiveRotation);
        setVelocity(0, 0);
    } else {
        double x = positionData.x;
        double y = positionData.y;
        positionData.setAngle(std::atan2(ai->inputs->mouse.y - y, ai->inputs->mouse.x - x));
    }
}

void FallenAC::tick(int tick) {
    AbstractBoss::tick(tick);
}
