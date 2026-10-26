#pragma once

#include <cstdint>
#include <unordered_map>

#include "coder/Reader.h"
#include "sim/Entity.h"

class Simulation {
public:
    std::unordered_map<uint32_t, Entity> entities;
    uint32_t tick = 0;
    double lastUpdateNow = -1e18;
    int cameraId = -1;
    int arenaId = -1;
    // Set on creates/deletes or hierarchy/order updates; gates scene rebuilds.
    bool sceneDirty = true;
    // Per-packet counts from the last update (ren_debug_info).
    int lastCreated = 0, lastUpdated = 0, lastDeleted = 0;

    void decodeUpdate(Reader& r, double now);
    Entity* get(EntityId ref);
    Entity* getCamera() { return find(cameraId); }
    Entity* getArena() { return find(arenaId); }

private:
    Entity* find(int id) {
        if (id < 0) return nullptr;
        auto it = entities.find((uint32_t)id);
        return it == entities.end() ? nullptr : &it->second;
    }

    static EntityId entid(Reader& r);
    void decodeCreation(Reader& r, Entity& e, double now);
    void decodeEntityUpdate(Reader& r, Entity& e, double now);
};
