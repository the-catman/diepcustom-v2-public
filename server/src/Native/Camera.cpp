#include "Native/Camera.h"
#include "Native/Manager.h"
#include "Native/UpcreateCompiler.h"
#include "Physics/PackedEntitySet.h"
#include "Physics/CollisionManager.h"
#include "Coder/Writer.h"
#include "Entity/Object.h"
#include "Entity/Tank/TankBody.h"
#include "Const/Achievements.h"
#include "Const/TankDefinitions.h"
#include "Const/Enums.h"
#include "Client.h"
#include "Game.h"
#include "Util.h"
#include "Config.h"

#include <cmath>

CameraEntity::CameraEntity(GameServer* game)
    : Entity(game, EntityCategory::Camera), cameraData(this), relationsData(this) {}

int CameraEntity::calculateStatCount(double level) {
    if (level <= 0) return 0;
    if (level <= 28) return (int)level - 1;
    return (int)std::floor(level / 3) + 18;
}

void CameraEntity::setLevel(double level) {
    double previousLevel = cameraData.level;

    cameraData.setLevel(level);
    cameraData.setLevelbarMax(level < config::maxPlayerLevel ? 1 : 0);

    double levelScore = levelToScore((int)level);
    bool isMaxLevel = level <= config::maxPlayerLevel;
    Entity* player = cameraData.player;

    if (isMaxLevel) cameraData.setScore(levelScore);

    Client* c = getClient();
    if (c && c->inputs->isPossessing) return;

    if (TankBody::isTank(player)) {
        TankBody* t = (TankBody*)player;
        double scaleFactor = std::pow(1.01, level - previousLevel);
        t->scale(scaleFactor);
        t->calculateStatData();
        if (isMaxLevel) {
            t->scoreData.setScore(levelScore);
            t->scoreReward = levelScore;
        }
    }

    int statIncrease = calculateStatCount(level) - calculateStatCount(previousLevel);
    cameraData.setStatsAvailable(cameraData.statsAvailable + statIncrease);

    TankDefinition* def = getTankById(cameraData.tank);
    setFieldFactor(def ? def->fieldFactor : 1);
    calculateLevelData();

    if (level > previousLevel) achievements::event(c, "levelUp", {{"level", level}, {"class", (int)cameraData.tank}});
}

void CameraEntity::setFieldFactor(double fieldFactor) {
    cameraData.setFOV((0.55 * fieldFactor) / std::pow(1.01, (cameraData.level - 1) / 2));
}

void CameraEntity::addScore(double score) {
    cameraData.setScore(cameraData.score + score);
    Entity* player = cameraData.player;
    if (player && player->getScoreData()) player->getScoreData()->setScore(player->getScoreData()->score + score);
    calculateLevelData();

    achievements::event(getClient(), "score", {{"total", cameraData.score}, {"delta", score}, {"class", (int)cameraData.tank}});
}

void CameraEntity::setScore(double score) {
    cameraData.setScore(score);
    Entity* player = cameraData.player;
    if (player && player->getScoreData()) player->getScoreData()->setScore(score);
    calculateLevelData();
}

void CameraEntity::addStat(int statId, double amount) {
    cameraData.statLevels.set(statId, cameraData.statLevels.values[statId] + amount);
    Entity* player = cameraData.player;
    if (TankBody::isTank(player)) ((TankBody*)player)->calculateStatData();

    achievements::event(getClient(), "statUpgraded", {
        {"id", statId},
        {"isMaxLevel", cameraData.statLevels.values[statId] >= cameraData.statLimits.values[statId]},
    });
}

void CameraEntity::setStat(int statId, double amount) {
    cameraData.statLevels.set(statId, amount);
    Entity* player = cameraData.player;
    if (TankBody::isTank(player)) ((TankBody*)player)->calculateStatData();
}

void CameraEntity::calculateLevelData() {
    Entity* player = cameraData.player;
    if (!TankBody::isTank(player)) return;

    double score = cameraData.score;
    double newLevel = cameraData.level;
    while (newLevel < (double)levelToScoreTable.size() && score - levelToScore((int)newLevel + 1) >= 0) newLevel += 1;

    if (newLevel != cameraData.level) {
        setLevel(newLevel);
        cameraData.setScore(score);
        ((TankBody*)player)->scoreData.setScore(score);
    }

    if (newLevel < (double)levelToScoreTable.size()) {
        double levelScore = levelToScore((int)cameraData.level);
        cameraData.setLevelbarMax(levelToScore((int)cameraData.level + 1) - levelScore);
        cameraData.setLevelbarProgress(score - levelScore);
    }
}

void CameraEntity::tick(int tick) {
    (void)tick;
    if (Entity::exists(cameraData.player)) {
        Entity* focus = cameraData.player;
        if (!(cameraData.flags & CameraFlags::usesCameraCoords) && ObjectEntity::isObject(focus)) {
            ObjectEntity* obj = (ObjectEntity*)focus;
            cameraData.setCameraX(obj->rootParent->positionData.x);
            cameraData.setCameraY(obj->rootParent->positionData.y);
        }
    } else {
        cameraData.setFlags(cameraData.flags | CameraFlags::usesCameraCoords);
    }
}

ClientCamera::ClientCamera(GameServer* game, Client* client) : CameraEntity(game), client(client) {
    cameraData.respawnLevel = cameraData.level = cameraData.score = 1;
    cameraData.FOV = 0.35;
    relationsData.team = this;
}

void ClientCamera::addToView(Entity* entity) {
    view.push_back(entity);
    game->entities->viewStamp[entity->id] = game->entities->stampCounter;
}

namespace {
    void snapshotEntity(EntityManager::ViewEntry& v, Entity* entity) {
        using V = EntityManager::ViewEntry;
        int state = entity->entityState;
        uint8_t flags = 0;
        if (entity->hash == 0) flags |= V::Dead;
        if (state & EntityStateFlags::needsCreate) flags |= V::NeedsCreate;
        if (state & EntityStateFlags::needsDelete) flags |= V::NeedsDelete;
        if (state & EntityStateFlags::needsUpdate) flags |= V::NeedsUpdate;
        if (entity->category == EntityCategory::Object) {
            ObjectEntity* obj = (ObjectEntity*)entity;
            flags |= V::Object;
            if (!obj->children.empty() && !obj->isChild) flags |= V::Parent;
            if (obj->styleData.color == Color::Tank) {
                flags |= V::TankColor;
                v.team = obj->relationsData.team.get();
            }
            if (obj->styleData.opacity == 0 && !obj->deletionAnimation) flags |= V::Hidden;
            v.root = obj->rootParent->id;
            const PhysicsData& physics = obj->physicsData;
            double halfW = physics.sides == 2 ? physics.size / 2 : physics.size;
            double halfH = physics.sides == 2 ? physics.width / 2 : physics.size;
            v.minX = obj->positionData.x - halfW;
            v.maxX = obj->positionData.x + halfW;
            v.minY = obj->positionData.y - halfH;
            v.maxY = obj->positionData.y + halfH;
        }
        v.hash = entity->preservedHash;
        v.flags = flags;
    }
}

void ClientCamera::prepareViews(EntityManager* entities) {
    std::vector<EntityManager::ViewEntry>& table = entities->viewEntries;
    int count = entities->lastId + 1;
    for (int id = 0; id < count; ++id) {
        if (Entity* entity = entities->inner[id]) snapshotEntity(table[id], entity);
        else table[id].hash = 0;
    }
    for (int id = count; id < entities->viewEntryCount; ++id) table[id].hash = 0;
    entities->viewEntryCount = count;
}

// Compiled bytes depend on the viewer only through swapColor; key the cache by (id, sameTeam).
int ClientCamera::cacheSlot(int id) {
    const EntityManager::ViewEntry& v = game->entities->viewEntries[id];
    if (!(v.flags & EntityManager::ViewEntry::TankColor)) return id << 1;
    return (id << 1) | (v.team == viewTeam ? 1 : 0);
}

void ClientCamera::compileCreationCached(Writer& w, int id) {
    EntityManager::CompileCache& cache = game->entities->createCache;
    int slot = cacheSlot(id), tick = game->tick;
    if (auto* e = cache.find(slot, tick)) { w.bytes(cache.arena.data() + e->start, e->size); return; }
    size_t start = w.mark();
    compileCreation(this, w, game->entities->inner[id]);
    cache.store(slot, tick, w.data() + start, w.size() - start);
}

void ClientCamera::compileUpdateCached(Writer& w, int id) {
    EntityManager::CompileCache& cache = game->entities->updateCache;
    int slot = cacheSlot(id), tick = game->tick;
    if (auto* e = cache.find(slot, tick)) { w.bytes(cache.arena.data() + e->start, e->size); return; }
    size_t start = w.mark();
    compileUpdate(this, w, game->entities->inner[id]);
    cache.store(slot, tick, w.data() + start, w.size() - start);
}

void ClientCamera::updateView(int tick) {
    using V = EntityManager::ViewEntry;
    WSWriterStream w = client->write();
    w.u8(ClientBound::Update).vu(tick);

    EntityManager* entities = game->entities;
    uint32_t stamp = ++entities->stampCounter;
    if (stamp == 0) {
        std::fill(entities->viewStamp.begin(), entities->viewStamp.end(), 0u);
        std::fill(entities->rangeStamp.begin(), entities->rangeStamp.end(), 0u);
        stamp = entities->stampCounter = 1;
    }
    std::vector<uint32_t>& rangeStamp = entities->rangeStamp;
    std::vector<uint32_t>& viewStamp = entities->viewStamp;
    std::vector<V>& table = entities->viewEntries;
    // This camera ticked after prepareViews.
    snapshotEntity(table[this->id], this);

    deletes.clear();
    updates.clear();
    creations.clear();
    entitiesInRange.clear();

    Entity* player = cameraData.player;
    RelationsData* playerRelations = player ? player->getRelationsData() : nullptr;
    viewTeam = playerRelations ? playerRelations->team.get() : nullptr;

    double fov = cameraData.FOV;
    double width = (1920 / fov) / 1.5;
    double height = (1080 / fov) / 1.5;

    PackedEntitySet& near = entities->collisionManager->retrieve(cameraData.cameraX, cameraData.cameraY, width, height);

    double l = cameraData.cameraX - width;
    double r = cameraData.cameraX + width;
    double t = cameraData.cameraY - height;
    double b = cameraData.cameraY + height;
    for (size_t i = 0; i < near.data.size(); ++i) {
        uint32_t chunk = near.data[i];
        while (chunk) {
            uint32_t bitValue = chunk & (0u - chunk);
            int bitIdx = 31 - __builtin_clz(bitValue);
            chunk ^= bitValue;
            int id = 32 * (int)i + bitIdx;

            const V& v = table[id];
            if (v.minX < r && v.maxY > t && v.maxX > l && v.minY < b) {
                if (!(v.flags & V::Hidden) && entities->inner[id] != player) {
                    entitiesInRange.push_back(id);
                    rangeStamp[id] = stamp;
                }
            }
        }
    }

    for (int id : entities->globalEntities) {
        if (rangeStamp[id] != stamp) { entitiesInRange.push_back(id); rangeStamp[id] = stamp; }
    }

    if (Entity::exists(player)) {
        entitiesInRange.push_back(player->id);
        rangeStamp[player->id] = stamp;
    }

    for (size_t i = 0; i < view.size(); ++i) {
        int id = view[i].rawId();
        uint32_t hash = view[i].rawHash();
        if (hash == 0 || table[id].hash != hash) {
            // freed during an aborted tick; the client is still owed the delete
            deletes.push_back({id, hash, (int)i});
            continue;
        }
        uint8_t flags = table[id].flags;
        if ((flags & V::Object) && rangeStamp[table[id].root] != stamp) {
            deletes.push_back({id, hash, (int)i});
            continue;
        }
        if (flags & V::Dead) {
            deletes.push_back({id, hash, (int)i});
        } else if (flags & V::NeedsCreate) {
            if (flags & V::NeedsDelete) deletes.push_back({id, hash, -1});
            creations.push_back(id);
        } else if (flags & V::NeedsUpdate) {
            updates.push_back(id);
        }
    }

    w.vu((int32_t)deletes.size());
    for (size_t i = 0; i < deletes.size(); ++i) w.entidRaw(deletes[i].hash, deletes[i].id);
    // viewIdx ascending; remove back-to-front so removeFast's swap never moves a pending index.
    for (int i = (int)deletes.size(); --i >= 0;) {
        if (deletes[i].viewIdx >= 0) util::removeFast(view, (std::ptrdiff_t)deletes[i].viewIdx);
    }

    if (view.empty()) {
        Entity* arena = (Entity*)game->arena;
        creations.push_back(arena->id);
        creations.push_back(this->id);
        view.push_back(arena);
        view.push_back(this);
    }

    for (size_t i = 0; i < view.size(); ++i) viewStamp[view[i].rawId()] = stamp;

    for (int id : entities->otherEntities) {
        if (viewStamp[id] != stamp) {
            Entity* entity = entities->inner[id];
            if (!entity) continue;
            if (entity->getCameraData()) continue;
            creations.push_back(id);
            addToView(entity);
        }
    }

    for (int id : entitiesInRange) {
        bool parent = table[id].flags & V::Parent;
        if (viewStamp[id] != stamp) {
            ObjectEntity* entity = (ObjectEntity*)entities->inner[id];
            creations.push_back(id);
            addToView(entity);
            if (parent) {
                for (ObjectEntity* child : entity->children) {
                    view.push_back(child);
                    creations.push_back(child->id);
                    viewStamp[child->id] = stamp;
                }
            }
        } else if (parent) {
            ObjectEntity* entity = (ObjectEntity*)entities->inner[id];
            for (ObjectEntity* child : entity->children) {
                if (viewStamp[child->id] != stamp) {
                    for (ObjectEntity* c2 : entity->children) {
                        view.push_back(c2);
                        creations.push_back(c2->id);
                        viewStamp[c2->id] = stamp;
                    }
                    break;
                }
            }
        }
    }

    w.vu((int32_t)(creations.size() + updates.size()));
    for (int id : updates) compileUpdateCached(w, id);
    for (int id : creations) compileCreationCached(w, id);

    w.send();
}

void ClientCamera::tick(int tick) {
    CameraEntity::tick(tick);

    if (!Entity::exists(cameraData.player)) {
        if (Entity::exists(spectatee)) {
            PositionData& pos = spectatee->rootParent->positionData;
            cameraData.setCameraX(pos.x);
            cameraData.setCameraY(pos.y);
            cameraData.setFlags(cameraData.flags | CameraFlags::usesCameraCoords);
        }
    }
    updateView(tick);
}
