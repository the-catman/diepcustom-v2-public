#pragma once

#include "Entity/Boss/AbstractBoss.h"

class FallenAC : public AbstractBoss {
public:
    explicit FallenAC(GameServer* game);
    void tick(int tick) override;

protected:
    void moveAroundMap() override;
};
