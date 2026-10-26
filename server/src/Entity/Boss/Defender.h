#pragma once

#include "Entity/Boss/AbstractBoss.h"

class Defender : public AbstractBoss {
public:
    explicit Defender(GameServer* game);
    void tick(int tick) override;
};
