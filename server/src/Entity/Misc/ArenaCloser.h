#pragma once

#include "Entity/Tank/TankBody.h"
#include "Entity/AI.h"

class ArenaCloser : public TankBody {
public:
    static constexpr double BASE_SIZE = 175;

    AI* ai;

    explicit ArenaCloser(GameServer* game);
    ~ArenaCloser() override { delete ai; }

    void tick(int tick) override;
};
