#pragma once

#include "Entity/Boss/AbstractBoss.h"

class FallenSpike : public AbstractBoss {
public:
    explicit FallenSpike(GameServer* game);
    void tick(int tick) override;

protected:
    void moveAroundMap() override;
};
