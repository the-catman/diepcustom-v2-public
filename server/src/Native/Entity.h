#pragma once

#include <cstdint>
#include <string>

class GameServer;

struct RelationsData;
struct BarrelData;
struct PhysicsData;
struct HealthData;
struct ArenaData;
struct NameData;
struct CameraData;
struct PositionData;
struct StyleData;
struct ScoreData;
struct TeamData;

namespace EntityStateFlags {
    constexpr int needsUpdate = 1 << 0;
    constexpr int needsCreate = 1 << 1;
    constexpr int needsDelete = 1 << 2;
}

// Manager classification; the subclass passes its category up the constructor chain.
enum class EntityCategory { Object, Camera, Other };

// Base of everything; wire field groups are reached via the virtual getXData() accessors.
class Entity {
public:
    static bool exists(Entity* entity) { return entity && entity->hash != 0; }

    int entityState = 0;
    uint32_t entityTags = 0;
    GameServer* game;
    EntityCategory category;
    int id = -1;
    uint32_t hash = 0;
    uint32_t preservedHash = 0;

    explicit Entity(GameServer* game, EntityCategory category = EntityCategory::Other);
    virtual ~Entity() = default;

    void wipeState();
    virtual void delete_();
    virtual void tick(int tick) {}

    virtual RelationsData* getRelationsData() { return nullptr; }
    virtual BarrelData* getBarrelData() { return nullptr; }
    virtual PhysicsData* getPhysicsData() { return nullptr; }
    virtual HealthData* getHealthData() { return nullptr; }
    virtual ArenaData* getArenaData() { return nullptr; }
    virtual NameData* getNameData() { return nullptr; }
    virtual CameraData* getCameraData() { return nullptr; }
    virtual PositionData* getPositionData() { return nullptr; }
    virtual StyleData* getStyleData() { return nullptr; }
    virtual ScoreData* getScoreData() { return nullptr; }
    virtual TeamData* getTeamData() { return nullptr; }
};
