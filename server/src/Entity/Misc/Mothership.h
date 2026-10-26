#pragma once

#include "Entity/Tank/TankBody.h"
#include "Entity/AI.h"

class Mothership : public TankBody {
public:
    AI* ai;
    int possessionStartTick = -1;

    explicit Mothership(GameServer* game);
    ~Mothership() override { delete ai; }

    void onDeath(LivingEntity* killer) override;
    void delete_() override;
    void tick(int tick) override;
};
