#include "Entity/Shape/Square.h"
#include "Game.h"
#include "Config.h"
#include "Const/Enums.h"
#include "Util.h"

#include <cmath>

Square::Square(GameServer* game, bool shiny) : AbstractShape(game) {
    nameData.setName("Square");
    healthData.health = healthData.maxHealth = 10;
    physicsData.size  = 55 * M_SQRT1_2;
    physicsData.sides = 4;
    styleData.color   = shiny ? Color::Shiny : Color::EnemySquare;

    damagePerTick = 2;
    scoreReward   = 10;
    isShiny       = shiny;

    if (shiny) {
        scoreReward *= 100;
        healthData.health = healthData.maxHealth *= 10;
        entityTags |= EntityTags::isShiny;
    }

    arenaMobID = "square";
}

Square* Square::makeRandom(GameServer* game) {
    return new Square(game, util::randomFloat() < config::shinyChance);
}
