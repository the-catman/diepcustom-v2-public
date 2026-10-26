#pragma once

#include <cstdint>
#include <vector>

#include "Native/Entity.h"
#include "Native/FieldGroups.h"

class GameServer;
class Client;
class Writer;
class ObjectEntity;
class EntityManager;

class CameraEntity : public Entity {
public:
    CameraData cameraData;
    RelationsData relationsData;
    Ref<ObjectEntity> spectatee;

    explicit CameraEntity(GameServer* game);

    CameraData* getCameraData() override { return &cameraData; }
    RelationsData* getRelationsData() override { return &relationsData; }

    static int calculateStatCount(double level);

    void setLevel(double level);
    virtual Client* getClient() { return nullptr; }
    void setFieldFactor(double fieldFactor);
    void addScore(double score);
    void setScore(double score);
    void addStat(int statId, double amount);
    void setStat(int statId, double amount);
    void calculateLevelData();
    void tick(int tick) override;
};

// Owns the client's stats/GUI data and compiles entity diffs to send to it.
class ClientCamera : public CameraEntity {
public:
    Client* client;

    ClientCamera(GameServer* game, Client* client);

    Client* getClient() override { return client; }
    void tick(int tick) override;

    // Fills EntityManager::viewEntries; runs once per tick before the cameras tick.
    static void prepareViews(EntityManager* entities);

private:
    // Refs, not raw pointers: an aborted tick reclaims entities before views purge them.
    std::vector<Ref<Entity>> view;

    // Scratch reused across ticks; membership tests use EntityManager's stamp arrays.
    struct DeleteRecord { int id; uint32_t hash; int viewIdx; };
    std::vector<DeleteRecord> deletes;
    std::vector<int> updates;
    std::vector<int> creations;
    std::vector<int> entitiesInRange;
    // The viewing player's team, resolved once per updateView for cacheSlot.
    Entity* viewTeam = nullptr;

    void addToView(Entity* entity);
    void updateView(int tick);
    int cacheSlot(int id);
    void compileCreationCached(Writer& w, int id);
    void compileUpdateCached(Writer& w, int id);
};
