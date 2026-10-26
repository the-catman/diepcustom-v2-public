#pragma once

#include <cstdint>
#include <functional>
#include <vector>

#include "Physics/CollisionManager.h"
#include "Physics/PackedEntitySet.h"

class GameServer;
class ObjectEntity;

class HashGrid : public CollisionManager {
public:
    explicit HashGrid(GameServer* game);

    void preTick(int tick) override;
    void postTick(int tick) override;
    void insert(ObjectEntity* entity) override;
    ObjectEntity* getFirstMatch(double centerX, double centerY, double halfWidth, double halfHeight,
                                const std::function<bool(ObjectEntity*)>& predicate) override;
    PackedEntitySet& retrieve(double centerX, double centerY, double halfWidth, double halfHeight) override;
    void forEachCollisionPair(const std::function<void(ObjectEntity*, ObjectEntity*)>& callback) override;

private:
    static constexpr int CELL_SHIFT = 8;
    static constexpr int CELL_SIZE = 1 << CELL_SHIFT;
    static constexpr int MAX_ENTITY_COUNT = 1 << 14;

    GameServer* game;
    PackedEntitySet resultSet;
    int lastQueryId = 0;
    std::vector<uint16_t> queryIdMap;

    bool isLocked = true;
    int gridW = 1;
    int gridH = 1;
    double gameLeftX = 0;
    double gameTopY = 0;

    // Out-of-bounds entities (arena closers, padding) clamp into edge cells.
    int cellX(double x) const {
        int c = (int32_t)(x - gameLeftX) >> CELL_SHIFT;
        return c < 0 ? 0 : (c >= gridW ? gridW - 1 : c);
    }
    int cellY(double y) const {
        int c = (int32_t)(y - gameTopY) >> CELL_SHIFT;
        return c < 0 ? 0 : (c >= gridH ? gridH - 1 : c);
    }

    // Flat cell grid, cleared per tick via usedCells; inner vectors keep capacity.
    std::vector<std::vector<int>> cells;
    std::vector<int> usedCells;

    // Min cell of each AABB; a pair is only reported in its canonical shared cell.
    std::vector<int16_t> cellMinX;
    std::vector<int16_t> cellMinY;
};
