#include "Entity/Boss/Defender.h"
#include "Native/Arena.h"
#include "Game.h"
#include "Entity/Tank/Barrel.h"
#include "Entity/Tank/AutoTurret.h"
#include "Const/Enums.h"
#include "Const/TankDefinitions.h"
#include "Util.h"

#include <cmath>

static BarrelDefinition MountedTurretDefinition = {
    0,      // angle
    0,      // offset
    {},     // distance
    55,     // size
    42 * 0.7, // width
    0.01,   // delay
    1,      // reload
    0.3,    // recoil
    false,  // isTrapezoid
    0,      // trapezoidDirection
    "",     // addon
    {},     // droneCount
    {},     // canControlDrones
    {},     // forceFire
    {},     // color
    {
        "bullet",
        1,      // sizeRatio
        5.75,   // health
        1.2,    // damage
        2.46,   // speed
        1,      // scatterRate
        1,      // lifeLength
        1,      // absorbtionFactor
        Color::Neutral,  // color
        {}               // sides
    }
};

static BarrelDefinition TrapperDefinition = {
    0,      // angle (overridden per barrel)
    0,      // offset
    {},     // distance
    120,    // size
    71.4,   // width
    0,      // delay
    5,      // reload
    2,      // recoil
    false,  // isTrapezoid
    0,      // trapezoidDirection
    "trapLauncher", // addon
    {},     // droneCount
    {},     // canControlDrones
    true,   // forceFire
    {},     // color
    {
        "trap",
        0.8,  // sizeRatio
        12.5, // health
        4,    // damage
        5,    // speed
        1,    // scatterRate
        8,    // lifeLength
        1,    // absorbtionFactor
        Color::Neutral,  // color
        {}               // sides
    }
};

static constexpr double DEFENDER_SIZE = 150;

Defender::Defender(GameServer* game) : AbstractBoss(game) {
    this->movementSpeed = 0.2;

    nameData.setName("Defender");
    styleData.setColor(Color::EnemyTriangle);
    relationsData.setTeam(game->arena);

    ai->viewRange = 0;
    ai->passiveRotation *= 2;

    physicsData.setSides(3);
    physicsData.setSize(DEFENDER_SIZE * M_SQRT1_2);

    int count = (int)physicsData.sides;
    double offset = 60.0 / (DEFENDER_SIZE * M_SQRT1_2);

    ownedBarrelDefs.reserve(count);
    for (int i = 0; i < count; ++i) {
        ownedBarrelDefs.push_back(TrapperDefinition);
        ownedBarrelDefs.back().angle = util::PI2 * ((double)i / count + 1.0 / (count * 2));
    }

    for (int i = 0; i < count; ++i) {
        barrels.push_back(new Barrel(this, &ownedBarrelDefs[i]));

        AutoTurret* base = new AutoTurret(this, &MountedTurretDefinition);
        base->influencedByOwnerInputs = true;

        double angle = util::PI2 * ((double)i / count);
        base->ai->inputs->mouse.setAngle(angle);
        base->positionData.y = physicsData.size * std::sin(angle) * offset;
        base->positionData.x = physicsData.size * std::cos(angle) * offset;
        base->physicsData.setFlags(base->physicsData.flags | PositionFlags::absoluteRotation);
    }
}

void Defender::tick(int tick) {
    AbstractBoss::tick(tick);

    if (ai->state != AIState::possessed) {
        positionData.setAngle(positionData.angle + ai->passiveRotation);
    }
}
