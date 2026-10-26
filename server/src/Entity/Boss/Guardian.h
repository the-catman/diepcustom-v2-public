#pragma once

#include "Entity/Boss/AbstractBoss.h"

class Guardian : public AbstractBoss {
public:
    explicit Guardian(GameServer* game);
    void tick(int tick) override;

protected:
    void moveAroundMap() override;
};
