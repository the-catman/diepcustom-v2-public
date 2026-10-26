#pragma once

#include "Entity/Shape/AbstractShape.h"

class Pentagon : public AbstractShape {
public:
    bool isAlpha;

    explicit Pentagon(GameServer* game, bool isAlpha = false, bool shiny = false);
    static Pentagon* makeRandom(GameServer* game, bool isAlpha = false);
};
