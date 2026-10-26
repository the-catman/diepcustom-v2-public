#pragma once

#include "Entity/Boss/AbstractBoss.h"

class FallenBooster : public AbstractBoss {
public:
    explicit FallenBooster(GameServer* game);
    void tick(int tick) override;

protected:
    void moveAroundMap() override;
};
