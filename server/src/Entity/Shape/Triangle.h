#pragma once

#include "Entity/Shape/AbstractShape.h"

class Triangle : public AbstractShape {
public:
    explicit Triangle(GameServer* game, bool shiny = false);
    static Triangle* makeRandom(GameServer* game);
};
