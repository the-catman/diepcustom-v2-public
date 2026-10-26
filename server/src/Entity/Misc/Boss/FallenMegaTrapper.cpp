#include "Entity/Misc/Boss/FallenMegaTrapper.h"
#include "Game.h"
#include "Entity/Tank/Barrel.h"
#include "Const/Enums.h"
#include "Const/TankDefinitions.h"

#include <cmath>

FallenMegaTrapper::FallenMegaTrapper(GameServer* game) : AbstractBoss(game) {
    this->movementSpeed = 1.0;

    nameData.setName("Fallen Mega Trapper");

    TankDefinition* def = TankDefinitions[Tank::MegaTrapper];
    ownedBarrelDefs.reserve(def->barrels.size());
    for (const BarrelDefinition& bd : def->barrels) {
        ownedBarrelDefs.push_back(bd);
        BarrelDefinition& d = ownedBarrelDefs.back();
        d.reload = 4;
        d.bullet.speed = 1.7;
        d.bullet.damage = 20;
        d.bullet.health = 20;
    }
    for (BarrelDefinition& d : ownedBarrelDefs) {
        barrels.push_back(new Barrel(this, &d));
    }

    scale(std::pow(1.01, 75 - 1));

    if (!barrels.empty()) ai->aimSpeed = barrels[0]->bulletAccel;
}

void FallenMegaTrapper::moveAroundMap() {
    if (ai->state == AIState::idle) {
        positionData.setAngle(positionData.angle + ai->passiveRotation);
        setVelocity(0, 0);
    } else {
        double x = positionData.x;
        double y = positionData.y;
        positionData.setAngle(std::atan2(ai->inputs->mouse.y - y, ai->inputs->mouse.x - x));
    }
}

void FallenMegaTrapper::tick(int tick) {
    AbstractBoss::tick(tick);
}
