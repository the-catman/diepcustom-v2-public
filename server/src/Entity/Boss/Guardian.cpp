#include "Entity/Boss/Guardian.h"
#include "Native/Arena.h"
#include "Game.h"
#include "Entity/Tank/Barrel.h"
#include "Const/Enums.h"
#include "Const/TankDefinitions.h"

#include <cmath>

static BarrelDefinition GuardianSpawnerDefinition = {
    M_PI,       // angle
    0,          // offset
    {},         // distance
    100,        // size
    71.4,       // width
    0,          // delay
    0.36,       // reload
    1,          // recoil
    true,       // isTrapezoid
    0,          // trapezoidDirection
    "",         // addon
    24,         // droneCount
    true,       // canControlDrones
    {},         // forceFire
    {},         // color
    {
        "drone",                    // type
        21.0 / (71.4 / 2),         // sizeRatio
        12.5,                       // health
        0.56,                       // damage
        1.7,                        // speed
        1,                          // scatterRate
        1.5,                        // lifeLength
        1,                          // absorbtionFactor
        {},                         // color
        {}                          // sides
    }
};

static constexpr double GUARDIAN_SIZE = 135;

Guardian::Guardian(GameServer* game) : AbstractBoss(game) {
    nameData.setName("Guardian");
    altName = "Guardian of the Pentagons";
    styleData.setColor(Color::EnemyCrasher);
    relationsData.setTeam(game->arena);
    physicsData.setSize(GUARDIAN_SIZE * M_SQRT1_2);
    physicsData.setSides(3);

    barrels.push_back(new Barrel(this, &GuardianSpawnerDefinition));
}

void Guardian::moveAroundMap() {
    AbstractBoss::moveAroundMap();
    positionData.setAngle(std::atan2(inputs->movement.y, inputs->movement.x));
}

void Guardian::tick(int tick) {
    AbstractBoss::tick(tick);
}
