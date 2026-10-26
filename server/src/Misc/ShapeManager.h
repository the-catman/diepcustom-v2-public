#pragma once

#include <vector>

#include "Native/Ref.h"

class ArenaEntity;
class GameServer;
class AbstractShape;

class ShapeManager {
public:
    explicit ShapeManager(ArenaEntity* arena);
    virtual ~ShapeManager() = default;

    void tick();
    void killAll();

protected:
    ArenaEntity* arena;
    GameServer* game;
    std::vector<Ref<AbstractShape>> shapes;

    virtual AbstractShape* spawnShape();
    virtual int wantedShapes() const { return 2000; }
    virtual int maxSpawnsPerTick() const { return 10; }
};
