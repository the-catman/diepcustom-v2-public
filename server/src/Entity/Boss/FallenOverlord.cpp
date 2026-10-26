#include "Entity/Boss/FallenOverlord.h"
#include "Game.h"
#include "Entity/Tank/Barrel.h"
#include "Const/Enums.h"
#include "Const/TankDefinitions.h"

#include <cmath>

FallenOverlord::FallenOverlord(GameServer* game) : AbstractBoss(game) {
    nameData.setName("Fallen Overlord");

    TankDefinition* def = TankDefinitions[Tank::Overlord];
    ownedBarrelDefs.reserve(def->barrels.size());
    for (const BarrelDefinition& bd : def->barrels) {
        ownedBarrelDefs.push_back(bd);
        BarrelDefinition& d = ownedBarrelDefs.back();
        d.droneCount = 7;
        d.reload = 0.36;
        d.bullet.sizeRatio = 0.5;
        d.bullet.speed = 1.7;
        d.bullet.damage = 0.56;
        d.bullet.health = 12.5;
    }
    for (BarrelDefinition& d : ownedBarrelDefs) {
        barrels.push_back(new Barrel(this, &d));
    }

    scale(std::pow(1.01, 75 - 1));
}

void FallenOverlord::tick(int tick) {
    AbstractBoss::tick(tick);

    if (ai->state != AIState::possessed) {
        positionData.setAngle(positionData.angle + ai->passiveRotation);
    }
}
