#pragma once

#include "Entity/Live.h"

class TeamBase : public LivingEntity {
public:
    TeamBase(GameServer* game, Entity* team, double x, double y, double width, double height,
             bool shielded = true, int droneSpawnerCount = 1, int droneCount = 12);

    void createDrones(int droneSpawnerCount, int droneCount);
    void tick(int tick) override;
};
