#include "Gamemodes/Maze.h"
#include "Game.h"
#include "Misc/BossManager.h"

static constexpr int GRID_SIZE = 40;
static constexpr double CELL_SIZE = 635.0;
static constexpr double ARENA_SIZE = GRID_SIZE * CELL_SIZE;

static const MazeGeneratorConfig MAZE_CONFIG = {
    .size               = GRID_SIZE,
    .baseSeedCount      = 45,
    .seedCountVariation = 30,
    .turnChance         = 0.2,
    .branchChance       = 0.2,
    .terminationChance  = 0.2,
};

MazeArena::MazeArena(GameServer* game)
    : ArenaEntity(game), mazeGenerator(MAZE_CONFIG)
{
    delete shapes;
    shapes = new MazeShapeManager(this);

    updateBounds(ARENA_SIZE, ARENA_SIZE);

    mazeGenerator.generate();
    mazeGenerator.placeWalls(this);

    delete bossManager;
    bossManager = nullptr;

    // The maze closes every 5 hours; the next instance generates a new one.
    GameServer* g = game;
    game->scheduleTimeout(5 * 60 * 60 * 1000.0, [g]() { g->arena->close(); });
}

bool MazeArena::isValidSpawnLocation(double x, double y) {
    MazeGenerator::GridCell cell = mazeGenerator.getGridCell(this, x, y);
    return !mazeGenerator.isCellOccupied(cell.gridX, cell.gridY);
}
