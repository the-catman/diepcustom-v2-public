#include "Misc/MazeGenerator.h"
#include "Native/Arena.h"
#include "Entity/Misc/MazeWall.h"
#include "Util.h"

#include <cmath>
#include <unordered_set>
#include <deque>
#include <algorithm>

static constexpr uint8_t MAZE_CELL_EMPTY = 0;
static constexpr uint8_t MAZE_CELL_WALL = 1;
static constexpr uint8_t MAZE_CELL_ACCESSIBLE = 2;
static constexpr uint8_t MAZE_CELL_PLACED_WALL = 3;

MazeGenerator::MazeGenerator(const MazeGeneratorConfig& cfg) : config(cfg) {
    maze.assign(cfg.size * cfg.size, MAZE_CELL_EMPTY);
}

void MazeGenerator::generate() {
    struct Seed { int x, y; };
    std::vector<Seed> seeds;

    int seedCount = config.baseSeedCount + (int)std::floor((util::randomFloat() - 0.5) * config.seedCountVariation);
    int maxSeedCount = config.baseSeedCount + config.seedCountVariation;

    for (int attempt = 0; attempt < 10000; ++attempt) {
        if ((int)seeds.size() >= seedCount) break;
        Seed s{(int)std::floor(util::randomFloat() * config.size - 1), (int)std::floor(util::randomFloat() * config.size - 1)};
        if (s.x <= 0 || s.y <= 0 || s.x >= config.size - 1 || s.y >= config.size - 1) continue;
        bool tooClose = false;
        for (const Seed& a : seeds) {
            if (std::abs(s.x - a.x) <= 3 && std::abs(s.y - a.y) <= 3) { tooClose = true; break; }
        }
        if (tooClose) continue;
        seeds.push_back(s);
        set(s.x, s.y, MAZE_CELL_WALL);
    }

    const int dirs[4][2] = { {-1,0},{1,0},{0,-1},{0,1} };

    for (int si = 0; si < (int)seeds.size(); ++si) {
        Seed seed = seeds[si]; // copy: seeds may reallocate via push_back below
        int dirIdx = (int)(util::randomFloat() * 4);
        int dx = dirs[dirIdx][0], dy = dirs[dirIdx][1];
        double termination = 1.0;
        while (termination >= config.terminationChance) {
            termination = util::randomFloat();
            seed.x += dx;
            seed.y += dy;
            if (seed.x <= 0 || seed.y <= 0 || seed.x >= config.size - 1 || seed.y >= config.size - 1) break;
            set(seed.x, seed.y, MAZE_CELL_WALL);

            if (util::randomFloat() <= config.branchChance) {
                if ((int)seeds.size() > maxSeedCount) continue;
                // pick perpendicular direction
                int perpIdx = (dirIdx < 2) ? (2 + (int)(util::randomFloat() * 2)) : (int)(util::randomFloat() * 2);
                Seed ns{seed.x + dirs[perpIdx][0], seed.y + dirs[perpIdx][1]};
                seeds.push_back(ns);
                set(seed.x, seed.y, MAZE_CELL_WALL);
            } else if (util::randomFloat() <= config.turnChance) {
                // turn perpendicular
                int perpIdx = (dirIdx < 2) ? (2 + (int)(util::randomFloat() * 2)) : (int)(util::randomFloat() * 2);
                dirIdx = perpIdx;
                dx = dirs[dirIdx][0]; dy = dirs[dirIdx][1];
            }
        }
    }

    // Sprinkle singular walls
    for (int i = 0; i < 10; ++i) {
        Seed s{(int)std::floor(util::randomFloat() * config.size - 1), (int)std::floor(util::randomFloat() * config.size - 1)};
        if (s.x <= 0 || s.y <= 0 || s.x >= config.size - 1 || s.y >= config.size - 1) continue;
        bool tooClose = false;
        for (int cy = 0; cy < config.size && !tooClose; ++cy) {
            for (int cx = 0; cx < config.size && !tooClose; ++cx) {
                if (get(cx, cy) == MAZE_CELL_WALL && std::abs(s.x - cx) <= 3 && std::abs(s.y - cy) <= 3)
                    tooClose = true;
            }
        }
        if (tooClose) continue;
        set(s.x, s.y, MAZE_CELL_WALL);
    }

    // BFS from (0,0) to mark accessible cells
    set(0, 0, MAZE_CELL_ACCESSIBLE);
    std::deque<int> queue;
    std::unordered_set<int> checked;
    queue.push_back(0);
    checked.insert(0);

    const int neighbors[4][2] = { {-1,0},{1,0},{0,-1},{0,1} };
    for (int iter = 0; iter < 3000 && !queue.empty(); ++iter) {
        int idx = queue.front(); queue.pop_front();
        int qx = idx % config.size, qy = idx / config.size;
        for (auto& n : neighbors) {
            int nx = qx + n[0], ny = qy + n[1];
            if (nx < 0 || ny < 0 || nx >= config.size || ny >= config.size) continue;
            if (get(nx, ny) != MAZE_CELL_EMPTY) continue;
            int ni = ny * config.size + nx;
            if (checked.count(ni)) continue;
            checked.insert(ni);
            queue.push_back(ni);
            set(nx, ny, MAZE_CELL_ACCESSIBLE);
        }
    }

    // Fill inaccessible empty cells with walls
    for (int y = 0; y < config.size; ++y) {
        for (int x = 0; x < config.size; ++x) {
            uint8_t v = get(x, y);
            if (v != MAZE_CELL_WALL && v != MAZE_CELL_ACCESSIBLE) set(x, y, MAZE_CELL_WALL);
        }
    }
}

std::vector<GridWall> MazeGenerator::convertToWalls() {
    // unplace previously placed walls
    for (int y = 0; y < config.size; ++y)
        for (int x = 0; x < config.size; ++x)
            if (get(x, y) == MAZE_CELL_PLACED_WALL) set(x, y, MAZE_CELL_WALL);

    std::vector<GridWall> walls;
    for (int x = 0; x < config.size; ++x) {
        for (int y = 0; y < config.size; ++y) {
            if (get(x, y) != MAZE_CELL_WALL) continue;
            GridWall chunk{x, y, 0, 1};
            while (get(x + chunk.width, y) == MAZE_CELL_WALL) {
                set(x + chunk.width, y, MAZE_CELL_PLACED_WALL);
                chunk.width++;
            }
            // expand height
            while (true) {
                bool canExpand = true;
                for (int i = 0; i < chunk.width; ++i) {
                    if (get(x + i, y + chunk.height) != MAZE_CELL_WALL) { canExpand = false; break; }
                }
                if (!canExpand) break;
                for (int i = 0; i < chunk.width; ++i) set(x + i, y + chunk.height, MAZE_CELL_PLACED_WALL);
                chunk.height++;
            }
            walls.push_back(chunk);
        }
    }
    return walls;
}

MazeGenerator::Vec2d MazeGenerator::scaleGridToArenaPosition(ArenaEntity* arena, int gridX, int gridY) const {
    double cellW = arena->width / config.size;
    double cellH = arena->height / config.size;
    return { gridX * cellW + arena->arenaData.leftX, gridY * cellH + arena->arenaData.topY };
}

MazeGenerator::GridPos MazeGenerator::scaleArenaToGridPosition(ArenaEntity* arena, double x, double y) const {
    double cellW = arena->width / config.size;
    double cellH = arena->height / config.size;
    return { (x + arena->width / 2) / cellW, (y + arena->height / 2) / cellH };
}

MazeGenerator::GridCell MazeGenerator::getGridCell(ArenaEntity* arena, double x, double y) const {
    GridPos gp = scaleArenaToGridPosition(arena, x, y);
    return { (int)std::floor(gp.gridX), (int)std::floor(gp.gridY) };
}

bool MazeGenerator::isCellOccupied(int x, int y) const {
    return get(x, y) == MAZE_CELL_PLACED_WALL;
}

void MazeGenerator::buildWallFromGridCoord(ArenaEntity* arena, int gx, int gy, int gw, int gh) {
    Vec2d minP = scaleGridToArenaPosition(arena, gx, gy);
    Vec2d maxP = scaleGridToArenaPosition(arena, gx + gw, gy + gh);
    double cx = (minP.x + maxP.x) / 2;
    double cy = (minP.y + maxP.y) / 2;
    double w = maxP.x - minP.x;
    double h = maxP.y - minP.y;
    new MazeWall(arena, cx, cy, w, h);
}

void MazeGenerator::placeWalls(ArenaEntity* arena) {
    std::vector<GridWall> walls = convertToWalls();
    for (const GridWall& w : walls) buildWallFromGridCoord(arena, w.x, w.y, w.width, w.height);
}
