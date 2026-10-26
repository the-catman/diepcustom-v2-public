#pragma once

#include "Native/Arena.h"
#include "Misc/ShapeManager.h"
#include "Misc/MazeGenerator.h"

class MazeShapeManager : public ShapeManager {
public:
    explicit MazeShapeManager(ArenaEntity* arena) : ShapeManager(arena) {}

protected:
    int wantedShapes() const override { return 1300; }
};

class MazeArena : public ArenaEntity {
public:
    static constexpr const char* GAMEMODE_ID = "maze";

    MazeGenerator mazeGenerator;

    explicit MazeArena(GameServer* game);

    bool isValidSpawnLocation(double x, double y) override;
};
