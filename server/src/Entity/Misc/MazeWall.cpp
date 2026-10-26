#include "Entity/Misc/MazeWall.h"
#include "Native/Arena.h"
#include "Const/Enums.h"

#include <algorithm>

MazeWall* MazeWall::newFromBounds(ArenaEntity* arena, double minX, double minY, double maxX, double maxY) {
    if (minX > maxX) std::swap(minX, maxX);
    if (minY > maxY) std::swap(minY, maxY);

    double width = maxX - minX;
    double height = maxY - minY;
    double centerX = (minX + maxX) / 2;
    double centerY = (minY + maxY) / 2;

    return new MazeWall(arena, centerX, centerY, width, height);
}

MazeWall::MazeWall(ArenaEntity* arena, double x, double y, double width, double height)
    : ObjectEntity(arena->game) {
    setGlobalEntity();

    positionData.x = x;
    positionData.y = y;

    physicsData.width = height;
    physicsData.size = width;
    physicsData.sides = 2;
    physicsData.setFlags(physicsData.flags | PhysicsFlags::isSolidWall);
    physicsData.pushFactor = 2;
    physicsData.absorbtionFactor = 0;

    relationsData.setTeam(arena);

    styleData.setBorderWidth(10);
    styleData.setColor(Color::Box);
}
