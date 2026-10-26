#pragma once

#include "Entity/Boss/AbstractBoss.h"

class Summoner : public AbstractBoss {
public:
    explicit Summoner(GameServer* game);
    void tick(int tick) override;
};
