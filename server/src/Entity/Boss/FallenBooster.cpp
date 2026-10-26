#include "Entity/Boss/FallenBooster.h"
#include "Game.h"
#include "Entity/Tank/Barrel.h"
#include "Const/Enums.h"
#include "Const/TankDefinitions.h"

#include <cmath>

FallenBooster::FallenBooster(GameServer* game) : AbstractBoss(game) {
    this->movementSpeed = 1.0;

    nameData.setName("Fallen Booster");

    TankDefinition* def = TankDefinitions[Tank::Booster];
    ownedBarrelDefs.reserve(def->barrels.size());
    for (const BarrelDefinition& bd : def->barrels) {
        ownedBarrelDefs.push_back(bd);
        BarrelDefinition& d = ownedBarrelDefs.back();
        d.bullet.speed = 1.7;
        d.bullet.health = 6.25;
        d.bullet.damage = bd.bullet.damage * 0.8;
    }
    for (BarrelDefinition& d : ownedBarrelDefs) {
        barrels.push_back(new Barrel(this, &d));
    }

    scale(std::pow(1.01, 75 - 1));
}

void FallenBooster::moveAroundMap() {
    double x = positionData.x;
    double y = positionData.y;

    if (ai->state == AIState::idle) {
        AbstractBoss::moveAroundMap();
        positionData.setAngle(std::atan2(inputs->movement.y, inputs->movement.x));
    } else {
        positionData.setAngle(std::atan2(ai->inputs->mouse.y - y, ai->inputs->mouse.x - x));
    }
}

void FallenBooster::tick(int tick) {
    AbstractBoss::tick(tick);
}
