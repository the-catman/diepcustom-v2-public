#include "Entity/Boss/Summoner.h"
#include "Native/Arena.h"
#include "Game.h"
#include "Entity/Tank/Barrel.h"
#include "Const/Enums.h"
#include "Util.h"

#include <cmath>

static BarrelDefinition SummonerSpawnerBase = {
    M_PI,   // angle (overridden per barrel)
    0,      // offset
    {},     // distance
    135,    // size
    71.4,   // width
    0,      // delay
    0.36,   // reload
    1,      // recoil
    true,   // isTrapezoid
    0,      // trapezoidDirection
    "",     // addon
    7,      // droneCount
    true,   // canControlDrones
    {},     // forceFire
    {},     // color
    {
        "drone",
        55.0 * M_SQRT1_2 / (71.4 / 2),  // sizeRatio
        12.5,                              // health
        0.56,                              // damage
        1.7,                               // speed
        1,                                 // scatterRate
        -1,                                // lifeLength
        1,                                 // absorbtionFactor
        Color::NecromancerSquare,          // color
        4.0                                // sides
    }
};

static constexpr double SUMMONER_SIZE = 150;

Summoner::Summoner(GameServer* game) : AbstractBoss(game) {
    nameData.setName("Summoner");
    styleData.setColor(Color::EnemySquare);
    relationsData.setTeam(game->arena);
    physicsData.setSize(SUMMONER_SIZE * M_SQRT1_2);
    physicsData.setSides(4);

    int count = (int)physicsData.sides;
    ownedBarrelDefs.reserve(count);
    for (int i = 0; i < count; ++i) {
        ownedBarrelDefs.push_back(SummonerSpawnerBase);
        ownedBarrelDefs.back().angle = util::PI2 * ((double)i / 4.0);
    }
    for (int i = 0; i < count; ++i) {
        barrels.push_back(new Barrel(this, &ownedBarrelDefs[i]));
    }
}

void Summoner::tick(int tick) {
    AbstractBoss::tick(tick);

    if (ai->state != AIState::possessed) {
        positionData.setAngle(positionData.angle + ai->passiveRotation);
    }
}
