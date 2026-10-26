#pragma once

#include "Entity/Shape/AbstractShape.h"

class Square : public AbstractShape {
public:
    explicit Square(GameServer* game, bool shiny = false);
    static Square* makeRandom(GameServer* game);
};
