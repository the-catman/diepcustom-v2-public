#pragma once

#include "Entity/Boss/AbstractBoss.h"

class FallenMegaTrapper : public AbstractBoss {
public:
    explicit FallenMegaTrapper(GameServer* game);
    void tick(int tick) override;

protected:
    void moveAroundMap() override;
};
