#pragma once

#include "Entity/Shape/AbstractShape.h"

class Crasher : public AbstractShape {
public:
    AI* ai;
    bool isLarge;

    explicit Crasher(GameServer* game, bool large = false);
    ~Crasher() override { delete ai; }

    void tick(int tick) override;

private:
    double targettingSpeed;
};
