#include "Entity/Shape/Pentagon.h"
#include "Game.h"
#include "Config.h"
#include "Const/Enums.h"
#include "Util.h"

#include <cmath>

Pentagon::Pentagon(GameServer* game, bool isAlpha, bool shiny)
    : AbstractShape(game,
                    AbstractShape::BASE_ROTATION / 2,
                    AbstractShape::BASE_ORBIT    / 2,
                    AbstractShape::BASE_VELOCITY / 2),
      isAlpha(isAlpha)
{
    nameData.setName(isAlpha ? "Alpha Pentagon" : "Pentagon");

    healthData.health = healthData.maxHealth = isAlpha ? 3000 : 100;
    physicsData.size  = (isAlpha ? 200 : 75) * M_SQRT1_2;
    physicsData.sides = 5;
    styleData.color   = shiny ? Color::Shiny : Color::EnemyPentagon;

    physicsData.absorbtionFactor = isAlpha ? 0.05 : 0.5;
    physicsData.pushFactor       = 11;

    isShiny       = shiny;
    damagePerTick = isAlpha ? 5 : 3;
    scoreReward   = isAlpha ? 3000 : 130;

    if (shiny) {
        scoreReward *= 100;
        healthData.health = healthData.maxHealth *= 10;
        entityTags |= EntityTags::isShiny;
    }

    arenaMobID = isAlpha ? "alphaPentagon" : "pentagon";
}

Pentagon* Pentagon::makeRandom(GameServer* game, bool isAlpha) {
    bool shiny = !isAlpha && (util::randomFloat() < config::shinyChance);
    return new Pentagon(game, isAlpha, shiny);
}
