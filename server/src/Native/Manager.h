#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "Native/Entity.h"

class GameServer;
class ObjectEntity;
class CollisionManager;
class AI;

class EntityManager {
public:
    GameServer* game;
    CollisionManager* collisionManager;

    int zIndex = 0;

    std::vector<int> cameras;
    std::vector<int> otherEntities;
    std::vector<int> globalEntities;
    // Never resized after construction: Refs hold its data pointer.
    std::vector<Entity*> inner;
    std::vector<AI*> AIs;

    // u32: a u8 generation counter would wrap to the deleted hash after 256 slot reuses.
    std::vector<uint32_t> hashTable;
    int lastId = -1;

    // Bytes compiled this tick by cache slot, shared by every view; one arena per tick.
    struct CompileCache {
        struct Entry { int tick = -1; uint32_t start = 0, size = 0; };
        std::vector<Entry> entries;
        std::vector<uint8_t> arena;
        int arenaTick = -1;

        const Entry* find(int slot, int tick) const {
            const Entry& e = entries[slot];
            return e.tick == tick ? &e : nullptr;
        }
        void store(int slot, int tick, const uint8_t* bytes, size_t size) {
            if (arenaTick != tick) { arena.clear(); arenaTick = tick; }
            entries[slot] = {tick, (uint32_t)arena.size(), (uint32_t)size};
            arena.insert(arena.end(), bytes, bytes + size);
        }
    };
    CompileCache createCache;
    CompileCache updateCache;

    // What views read about each entity, gathered before the cameras tick so each view scans
    // this table instead of the entities. While the cameras run, nothing but a camera's own
    // tick changes entities, and each camera refreshes its own entry.
    struct ViewEntry {
        enum : uint8_t {
            Dead = 1, NeedsCreate = 2, NeedsDelete = 4, NeedsUpdate = 8,
            Object = 16, Parent = 32, TankColor = 64, Hidden = 128,
        };
        double minX, maxX, minY, maxY;
        Entity* team; // for TankColor
        uint32_t hash; // preservedHash; 0 for an empty slot
        int root; // rootParent's id, for Object
        uint8_t flags;
    };
    std::vector<ViewEntry> viewEntries;
    int viewEntryCount = 0; // ids prepareViews filled

    // Membership stamps for view updates; bumping the counter invalidates both sets.
    std::vector<uint32_t> viewStamp;
    std::vector<uint32_t> rangeStamp;
    uint32_t stampCounter = 0;

    explicit EntityManager(GameServer* game);
    ~EntityManager();

    Entity* add(Entity* entity);
    void delete_(int id);
    // newEntity takes over oldEntity's wire identity; refs to the old resolve to the new.
    void replaceEntity(Entity* oldEntity, Entity* newEntity);
    void clear();
    void preTick(int tick);
    void postTick(int tick);
    void tick(int tick);
    void reclaim();

private:
    // Deleted entities move to graveyard and are freed by reclaim() at end of tick.
    std::vector<std::unique_ptr<Entity>> owned;
    std::vector<std::unique_ptr<Entity>> graveyard;
    // Ids given up by replaceEntity, held until reclaim(): the collision grid may still list
    // them this tick, and a reused id would hand queries an unrelated entity.
    std::vector<int> releasedIds;
    // Mirror of inner[id] == nullptr; add() finds the lowest free id by first-set-bit.
    std::vector<uint64_t> freeBits;
    void freeSlot(int id) { freeBits[id >> 6] |= 1ull << (id & 63); }
    void handleCollision(ObjectEntity* a, ObjectEntity* b);
};
