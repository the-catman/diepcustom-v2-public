#include "Entity/Shape/Triangle.h"
#include "Game.h"
#include "Config.h"
#include "Const/Enums.h"
#include "Util.h"

#include <cmath>

Triangle::Triangle(GameServer* game, bool shiny) : AbstractShape(game) {
    nameData.setName("Triangle");
    healthData.health = healthData.maxHealth = 30;
    physicsData.size  = 55 * M_SQRT1_2;
    physicsData.sides = 3;
    styleData.color   = shiny ? Color::Shiny : Color::EnemyTriangle;

    damagePerTick = 2;
    scoreReward   = 25;
    isShiny       = shiny;

    if (shiny) {
        scoreReward *= 100;
        healthData.health = healthData.maxHealth *= 10;
        entityTags |= EntityTags::isShiny;
    }

    arenaMobID = "triangle";
}

Triangle* Triangle::makeRandom(GameServer* game) {
    return new Triangle(game, util::randomFloat() < config::shinyChance);
}
