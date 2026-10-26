#pragma once

#include "Entity/Object.h"

class ArenaEntity;

class MazeWall : public ObjectEntity {
public:
    static MazeWall* newFromBounds(ArenaEntity* arena, double minX, double minY, double maxX, double maxY);

    MazeWall(ArenaEntity* arena, double x, double y, double width, double height);

    void tick(int tick) override {}
    void applyPhysics() override {}
};
