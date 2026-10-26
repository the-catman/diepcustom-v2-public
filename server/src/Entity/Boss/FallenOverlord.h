#pragma once

#include "Entity/Boss/AbstractBoss.h"

class FallenOverlord : public AbstractBoss {
public:
    explicit FallenOverlord(GameServer* game);
    void tick(int tick) override;
};
