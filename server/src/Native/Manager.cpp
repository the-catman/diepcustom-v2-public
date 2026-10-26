#include "Native/Manager.h"
#include "Native/FieldGroups.h"
#include "Physics/HashGrid.h"
#include "Native/Camera.h"
#include "Entity/Object.h"
#include "Entity/Live.h"
#include "Entity/AI.h"
#include "Game.h"
#include "Util.h"

#include <algorithm>
#include <stdexcept>

EntityManager::EntityManager(GameServer* game) : game(game) {
    inner.assign(16384, nullptr);
    owned.resize(16384);
    hashTable.assign(16384, 0);
    // Two slots per entity: compiled bytes differ only by viewer-team (swapColor).
    createCache.entries.resize(32768);
    updateCache.entries.resize(32768);
    viewEntries.resize(16384);
    viewStamp.assign(16384, 0);
    rangeStamp.assign(16384, 0);
    freeBits.assign(16384 / 64, ~0ull);
    collisionManager = new HashGrid(game);
}

EntityManager::~EntityManager() {
    delete collisionManager;
}

Entity* EntityManager::add(Entity* entity) {
    for (size_t word = 0; word < freeBits.size(); ++word) {
        uint64_t bits = freeBits[word];
        if (!bits) continue;
        int id = (int)(word * 64) + __builtin_ctzll(bits);
        freeBits[word] = bits & (bits - 1);

        entity->id = id;
        entity->hash = entity->preservedHash = (hashTable[id] += 1);
        inner[id] = entity;
        owned[id].reset(entity);

        if (entity->category == EntityCategory::Object) {
        } else if (entity->category == EntityCategory::Camera) {
            cameras.push_back(id);
        } else {
            otherEntities.push_back(id);
        }

        if (lastId < id) lastId = entity->id;
        return entity;
    }
    throw std::runtime_error("OOEI: Out Of Entity IDs");
}

void EntityManager::delete_(int id) {
    Entity* entity = inner[id];
    if (!entity) throw std::range_error("Deleting entity that isn't in the game?");
    if (entity->hash == 0) return; // already deleted this tick; slot freed at reclaim
    entity->hash = 0;

    if (entity->category == EntityCategory::Object) {
        // Objects keep no id list to prune.
    } else if (entity->getCameraData()) {
        auto it = std::find(cameras.begin(), cameras.end(), id);
        if (it != cameras.end()) util::removeFast(cameras, it - cameras.begin());
    } else {
        auto it = std::find(otherEntities.begin(), otherEntities.end(), id);
        if (it != otherEntities.end()) util::removeFast(otherEntities, it - otherEntities.begin());
    }

    // inner[id] keeps pointing at the deleted entity until reclaim, so handles resolve this tick.
    if (owned[id]) graveyard.push_back(std::move(owned[id]));
}

void EntityManager::replaceEntity(Entity* oldEntity, Entity* newEntity) {
    int oldId = oldEntity->id;
    int newId = newEntity->id;

    owned[newId].release();
    inner[newId] = nullptr;
    releasedIds.push_back(newId);

    newEntity->id = oldId;
    newEntity->hash = oldEntity->hash;
    newEntity->preservedHash = oldEntity->preservedHash;
    newEntity->entityState = EntityStateFlags::needsCreate | EntityStateFlags::needsDelete;

    oldEntity->hash = 0;
    // reclaim() frees it but won't null inner[oldId]: the slot no longer points at it
    graveyard.push_back(std::move(owned[oldId]));
    inner[oldId] = newEntity;
    owned[oldId].reset(newEntity);
}

void EntityManager::reclaim() {
    for (auto& e : graveyard) {
        if (e && inner[e->id] == e.get()) {
            inner[e->id] = nullptr;
            freeSlot(e->id);
        }
    }
    graveyard.clear();
    for (int id : releasedIds) freeSlot(id);
    releasedIds.clear();
}

void EntityManager::clear() {
    lastId = -1;
    collisionManager->postTick(game->tick);
    std::fill(hashTable.begin(), hashTable.end(), 0u);
    AIs.clear();
    otherEntities.clear();
    cameras.clear();

    for (size_t i = 0; i < inner.size(); ++i) {
        if (inner[i]) {
            inner[i]->hash = 0;
            inner[i] = nullptr;
        }
    }
    graveyard.clear();
    releasedIds.clear();
    owned.clear();
    owned.resize(16384);
    std::fill(freeBits.begin(), freeBits.end(), ~0ull);
}

void EntityManager::handleCollision(ObjectEntity* a, ObjectEntity* b) {
    if (!ObjectEntity::isColliding(a, b)) return;
    ObjectEntity::handleCollision(a, b);
    if (LivingEntity::isLive(a) && LivingEntity::isLive(b)) {
        LivingEntity::handleCollision((LivingEntity*)a, (LivingEntity*)b);
    }
}

void EntityManager::preTick(int tick) {
    collisionManager->preTick(tick);

    while (lastId >= 0 && !inner[lastId]) lastId -= 1;

    for (int id = 0; id <= lastId; ++id) {
        Entity* entity = inner[id];
        if (!Entity::exists(entity)) continue;
        if (entity->category == EntityCategory::Object) {
            ObjectEntity* obj = (ObjectEntity*)entity;
            if (obj->isPhysical) collisionManager->insert(obj);
        }
    }
}

void EntityManager::postTick(int tick) {
    collisionManager->postTick(tick);
    for (int id = 0; id <= lastId; ++id) {
        Entity* entity = inner[id];
        if (entity) entity->wipeState();
    }
}

void EntityManager::tick(int tick) {
    collisionManager->forEachCollisionPair([this](ObjectEntity* a, ObjectEntity* b) { handleCollision(a, b); });

    for (int id = 0; id <= lastId; ++id) {
        Entity* entity = inner[id];
        if (!entity || entity->hash == 0) continue;

        if (entity->category == EntityCategory::Camera) {
            continue;
        } else if (entity->category == EntityCategory::Object) {
            ObjectEntity* obj = (ObjectEntity*)entity;
            if (obj->isPhysical) obj->applyPhysics();
            if (!obj->isChild) obj->tick(tick);
        } else {
            entity->tick(tick);
        }
    }

    for (int i = (int)AIs.size(); --i >= 0;) {
        if (!Entity::exists(AIs[i]->owner)) {
            util::removeFast(game->entities->AIs, i);
            continue;
        }
        AIs[i]->tick(tick);
    }

    if (!cameras.empty()) ClientCamera::prepareViews(this);
    for (size_t i = 0; i < cameras.size(); ++i) {
        ((CameraEntity*)inner[cameras[i]])->tick(tick);
    }
}
