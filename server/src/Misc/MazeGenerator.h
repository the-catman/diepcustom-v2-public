#pragma once

#include <cstdint>
#include <vector>

class ArenaEntity;

struct MazeGeneratorConfig {
    int size;
    int baseSeedCount;
    int seedCountVariation;
    double turnChance;
    double branchChance;
    double terminationChance;
};

struct GridWall {
    int x, y, width, height;
};

class MazeGenerator {
public:
    MazeGeneratorConfig config;
    std::vector<uint8_t> maze;

    explicit MazeGenerator(const MazeGeneratorConfig& config);

    void generate();
    void placeWalls(ArenaEntity* arena);

    bool isCellOccupied(int x, int y) const;

    struct GridCell { int gridX; int gridY; };
    GridCell getGridCell(ArenaEntity* arena, double x, double y) const;

protected:
    // JS Uint8Array semantics: OOB read -> undefined (sentinel), OOB write -> ignored.
    uint8_t get(int x, int y) const {
        int idx = y * config.size + x;
        if (idx < 0 || idx >= (int)maze.size()) return 0xff;
        return maze[idx];
    }
    void set(int x, int y, uint8_t value) {
        int idx = y * config.size + x;
        if (idx < 0 || idx >= (int)maze.size()) return;
        maze[idx] = value;
    }

    std::vector<GridWall> convertToWalls();
    void buildWallFromGridCoord(ArenaEntity* arena, int gridX, int gridY, int gridW, int gridH);

    struct Vec2d { double x; double y; };
    Vec2d scaleGridToArenaPosition(ArenaEntity* arena, int gridX, int gridY) const;
    struct GridPos { double gridX; double gridY; };
    GridPos scaleArenaToGridPosition(ArenaEntity* arena, double x, double y) const;
};
