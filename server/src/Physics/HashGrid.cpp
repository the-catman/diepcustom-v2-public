#include "Physics/HashGrid.h"
#include "Native/FieldGroups.h"
#include "Native/Arena.h"
#include "Native/Manager.h"
#include "Entity/Object.h"
#include "Game.h"

#include <cmath>
#include <stdexcept>

HashGrid::HashGrid(GameServer* game)
    : game(game), queryIdMap(MAX_ENTITY_COUNT, 0), cellMinX(MAX_ENTITY_COUNT, 0), cellMinY(MAX_ENTITY_COUNT, 0) {}

void HashGrid::preTick(int tick) {
    (void)tick;
    ArenaEntity* arena = game->arena;
    gridW = ((int)arena->width + (CELL_SIZE - 1)) >> CELL_SHIFT;
    gridH = ((int)arena->height + (CELL_SIZE - 1)) >> CELL_SHIFT;
    if (gridW < 1) gridW = 1;
    if (gridH < 1) gridH = 1;
    if ((int)cells.size() < gridW * gridH) cells.resize(gridW * gridH);

    for (int idx : usedCells) cells[idx].clear();
    usedCells.clear();

    std::fill(queryIdMap.begin(), queryIdMap.end(), (uint16_t)0);
    lastQueryId = 0;
    gameLeftX = arena->arenaData.leftX;
    gameTopY = arena->arenaData.topY;
    isLocked = false;
}

void HashGrid::postTick(int tick) {
    (void)tick;
    isLocked = true;
}

void HashGrid::insert(ObjectEntity* entity) {
    if (isLocked) throw std::runtime_error("HashGrid is locked! Cannot insert() outside of tick");
    double sides = entity->physicsData.sides;
    double size = entity->physicsData.size;
    double width = entity->physicsData.width;
    double x = entity->positionData.x;
    double y = entity->positionData.y;
    bool isLine = sides == 2;
    double halfWidth = isLine ? size / 2 : size;
    double halfHeight = isLine ? width / 2 : size;

    int topX = cellX(x - halfWidth);
    int topY = cellY(y - halfHeight);
    int bottomX = cellX(x + halfWidth);
    int bottomY = cellY(y + halfHeight);

    cellMinX[entity->id] = (int16_t)topX;
    cellMinY[entity->id] = (int16_t)topY;

    for (int cy = topY; cy <= bottomY; ++cy) {
        int rowBase = cy * gridW;
        for (int cx = topX; cx <= bottomX; ++cx) {
            auto& cell = cells[rowBase + cx];
            if (cell.empty()) usedCells.push_back(rowBase + cx);
            cell.push_back(entity->id);
        }
    }
}

PackedEntitySet& HashGrid::retrieve(double centerX, double centerY, double halfWidth, double halfHeight) {
    if (isLocked) throw std::runtime_error("HashGrid is locked! Cannot retrieve() outside of tick");
    resultSet.clear();

    int startX = cellX(centerX - halfWidth);
    int startY = cellY(centerY - halfHeight);
    int endX = cellX(centerX + halfWidth);
    int endY = cellY(centerY + halfHeight);

    int queryId = lastQueryId == 0xffff ? 1 : lastQueryId + 1;
    lastQueryId = queryId;

    for (int y = startY; y <= endY; ++y) {
        int rowBase = y * gridW;
        for (int x = startX; x <= endX; ++x) {
            for (int entityId : cells[rowBase + x]) {
                if (queryIdMap[entityId] == queryId) continue;
                queryIdMap[entityId] = queryId;
                Entity* entity = game->entities->inner[entityId];
                if (!entity || entity->hash == 0) continue;
                resultSet.add(entityId);
            }
        }
    }
    return resultSet;
}

ObjectEntity* HashGrid::getFirstMatch(double centerX, double centerY, double halfWidth, double halfHeight,
                                      const std::function<bool(ObjectEntity*)>& predicate) {
    if (isLocked) throw std::runtime_error("HashGrid is locked! Cannot getFirstMatch() outside of tick");

    int startX = cellX(centerX - halfWidth);
    int startY = cellY(centerY - halfHeight);
    int endX = cellX(centerX + halfWidth);
    int endY = cellY(centerY + halfHeight);

    int queryId = lastQueryId == 0xffff ? 1 : lastQueryId + 1;
    lastQueryId = queryId;

    for (int y = startY; y <= endY; ++y) {
        int rowBase = y * gridW;
        for (int x = startX; x <= endX; ++x) {
            for (int entityId : cells[rowBase + x]) {
                if (queryIdMap[entityId] == queryId) continue;
                queryIdMap[entityId] = queryId;
                Entity* entity = game->entities->inner[entityId];
                if (!entity || entity->hash == 0) continue;
                ObjectEntity* obj = (ObjectEntity*)entity;
                if (predicate(obj)) return obj;
            }
        }
    }
    return nullptr;
}

void HashGrid::forEachCollisionPair(const std::function<void(ObjectEntity*, ObjectEntity*)>& callback) {
    if (isLocked) throw std::runtime_error("HashGrid is locked! Cannot forEachCollisionPair() outside of tick");

    auto& inner = game->entities->inner;
    for (int idx : usedCells) {
        auto& cell = cells[idx];
        if (cell.size() < 2) continue;
        int cx = idx % gridW;
        int cy = idx / gridW;

        for (size_t a = 0; a + 1 < cell.size(); ++a) {
            int eidA = cell[a];
            Entity* entityA = inner[eidA];
            if (!entityA || entityA->hash == 0) continue;
            int minXA = cellMinX[eidA];
            int minYA = cellMinY[eidA];

            for (size_t b = a + 1; b < cell.size(); ++b) {
                int eidB = cell[b];
                Entity* entityB = inner[eidB];
                if (!entityB || entityB->hash == 0) continue;

                int px = minXA > cellMinX[eidB] ? minXA : (int)cellMinX[eidB];
                int py = minYA > cellMinY[eidB] ? minYA : (int)cellMinY[eidB];
                if (px != cx || py != cy) continue;

                if (eidA < eidB) callback((ObjectEntity*)entityA, (ObjectEntity*)entityB);
                else callback((ObjectEntity*)entityB, (ObjectEntity*)entityA);
            }
        }
    }
}
