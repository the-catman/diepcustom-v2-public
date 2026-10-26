#pragma once

#include <cstdint>
#include <string>

#include "Native/Entity.h"
#include "Native/Ref.h"
#include "Const/Enums.h"


template <typename T>
struct ScoreboardTable {
    uint16_t dirty = 0;
    T values[10] = {};
    uint32_t* ownerDirty = nullptr;
    int fieldId = 0;
    Entity* entity = nullptr;

    void bind(const T& def, int fid, uint32_t* od, Entity* e) {
        fieldId = fid; ownerDirty = od; entity = e;
        for (int i = 0; i < 10; ++i) values[i] = def;
    }
    void wipe() { dirty = 0; }
    void set(int i, const T& v) {
        if (v == values[i]) return;
        dirty |= (uint16_t)(1u << i);
        values[i] = v;
        *ownerDirty |= (1u << fieldId);
        entity->entityState |= EntityStateFlags::needsUpdate;
    }
};

template <typename T>
struct CameraTable {
    uint8_t dirty = 0;
    T values[8] = {};
    uint32_t* ownerDirty = nullptr;
    int fieldId = 0;
    Entity* entity = nullptr;

    void bind(const T& def, int fid, uint32_t* od, Entity* e) {
        fieldId = fid; ownerDirty = od; entity = e;
        for (int i = 0; i < 8; ++i) values[i] = def;
    }
    void wipe() { dirty = 0; }
    void set(int i, const T& v) {
        if (v == values[i]) return;
        dirty |= (uint8_t)(1u << i);
        values[i] = v;
        *ownerDirty |= (1u << fieldId);
        entity->entityState |= EntityStateFlags::needsUpdate;
    }
};

#define FG_DIRTY(bit) do { dirty |= (1u << (bit)); entity->entityState |= EntityStateFlags::needsUpdate; } while (0)

struct RelationsData {
    uint32_t dirty = 0;
    Entity* entity;
    Ref<Entity> parent;
    Ref<Entity> owner;
    Ref<Entity> team;

    explicit RelationsData(Entity* e) : entity(e) {}
    void wipe() { dirty = 0; }
    void setParent(Entity* v) { if (v == parent) return; FG_DIRTY(0); parent = v; }
    void setOwner(Entity* v) { if (v == owner) return; FG_DIRTY(1); owner = v; }
    void setTeam(Entity* v) { if (v == team) return; FG_DIRTY(2); team = v; }
};

struct BarrelData {
    uint32_t dirty = 0;
    Entity* entity;
    uint32_t flags = 0;
    double reloadTime = 15;
    double trapezoidDirection = 0;

    explicit BarrelData(Entity* e) : entity(e) {}
    void wipe() { dirty = 0; }
    void setFlags(uint32_t v) { if (v == flags) return; FG_DIRTY(0); flags = v; }
    void setReloadTime(double v) { if (v == reloadTime) return; FG_DIRTY(1); reloadTime = v; }
    void setTrapezoidDirection(double v) { if (v == trapezoidDirection) return; FG_DIRTY(2); trapezoidDirection = v; }
};

struct PhysicsData {
    uint32_t dirty = 0;
    Entity* entity;
    uint32_t flags = 0;
    double sides = 0;
    double size = 0;
    double width = 0;
    double absorbtionFactor = 1;
    double pushFactor = 8;

    explicit PhysicsData(Entity* e) : entity(e) {}
    void wipe() { dirty = 0; }
    void setFlags(uint32_t v) { if (v == flags) return; FG_DIRTY(0); flags = v; }
    void setSides(double v) { if (v == sides) return; FG_DIRTY(1); sides = v; }
    void setSize(double v) { if (v == size) return; FG_DIRTY(2); size = v; }
    void setWidth(double v) { if (v == width) return; FG_DIRTY(3); width = v; }
    void setAbsorbtionFactor(double v) { if (v == absorbtionFactor) return; FG_DIRTY(4); absorbtionFactor = v; }
    void setPushFactor(double v) { if (v == pushFactor) return; FG_DIRTY(5); pushFactor = v; }
};

struct HealthData {
    uint32_t dirty = 0;
    Entity* entity;
    uint32_t flags = 0;
    double health = 1;
    double maxHealth = 1;

    explicit HealthData(Entity* e) : entity(e) {}
    void wipe() { dirty = 0; }
    void setFlags(uint32_t v) { if (v == flags) return; FG_DIRTY(0); flags = v; }
    void setHealth(double v) { if (v == health) return; FG_DIRTY(1); health = v; }
    void setMaxHealth(double v) { if (v == maxHealth) return; FG_DIRTY(2); maxHealth = v; }
};

struct ArenaData {
    uint32_t dirty = 0;
    Entity* entity;
    uint32_t flags = 2;
    double leftX = 0;
    double topY = 0;
    double rightX = 0;
    double bottomY = 0;
    double scoreboardAmount = 0;
    ScoreboardTable<std::string> scoreboardNames;
    ScoreboardTable<double> scoreboardScores;
    ScoreboardTable<int> scoreboardColors;
    ScoreboardTable<std::string> scoreboardSuffixes;
    ScoreboardTable<int> scoreboardTanks;
    double leaderX = 0;
    double leaderY = 0;
    double playersNeeded = 1;
    double ticksUntilStart = 250;

    explicit ArenaData(Entity* e) : entity(e) {
        scoreboardNames.bind("", 6, &dirty, e);
        scoreboardScores.bind(0, 7, &dirty, e);
        scoreboardColors.bind(Color::ScoreboardBar, 8, &dirty, e);
        scoreboardSuffixes.bind("", 9, &dirty, e);
        scoreboardTanks.bind(Tank::Basic, 10, &dirty, e);
    }
    void wipe() {
        dirty = 0;
        scoreboardNames.wipe(); scoreboardScores.wipe(); scoreboardColors.wipe();
        scoreboardSuffixes.wipe(); scoreboardTanks.wipe();
    }
    void setFlags(uint32_t v) { if (v == flags) return; FG_DIRTY(0); flags = v; }
    void setLeftX(double v) { if (v == leftX) return; FG_DIRTY(1); leftX = v; }
    void setTopY(double v) { if (v == topY) return; FG_DIRTY(2); topY = v; }
    void setRightX(double v) { if (v == rightX) return; FG_DIRTY(3); rightX = v; }
    void setBottomY(double v) { if (v == bottomY) return; FG_DIRTY(4); bottomY = v; }
    void setScoreboardAmount(double v) { if (v == scoreboardAmount) return; FG_DIRTY(5); scoreboardAmount = v; }
    void setLeaderX(double v) { if (v == leaderX) return; FG_DIRTY(11); leaderX = v; }
    void setLeaderY(double v) { if (v == leaderY) return; FG_DIRTY(12); leaderY = v; }
    void setPlayersNeeded(double v) { if (v == playersNeeded) return; FG_DIRTY(13); playersNeeded = v; }
    void setTicksUntilStart(double v) { if (v == ticksUntilStart) return; FG_DIRTY(14); ticksUntilStart = v; }
};

struct NameData {
    uint32_t dirty = 0;
    Entity* entity;
    uint32_t flags = 0;
    std::string name = "";

    explicit NameData(Entity* e) : entity(e) {}
    void wipe() { dirty = 0; }
    void setFlags(uint32_t v) { if (v == flags) return; FG_DIRTY(0); flags = v; }
    void setName(const std::string& v) { if (v == name) return; FG_DIRTY(1); name = v; }
};

struct CameraData {
    uint32_t dirty = 0;
    Entity* entity;
    uint32_t unusedClientId = 0;
    uint32_t flags = 1;
    Ref<Entity> player;
    double FOV = 0.35;
    double level = 1;
    int tank = Tank::MountedTurret;
    double levelbarProgress = 0;
    double levelbarMax = 0;
    double statsAvailable = 0;
    CameraTable<std::string> statNames;
    CameraTable<double> statLevels;
    CameraTable<double> statLimits;
    double cameraX = 0;
    double cameraY = 0;
    double score = 0;
    double respawnLevel = 0;
    std::string killedBy = "";
    double spawnTick = 0;
    double deathTick = -1;
    std::string tankOverride = "";
    double movementSpeed = 0;

    explicit CameraData(Entity* e) : entity(e) {
        statNames.bind("", 9, &dirty, e);
        statLevels.bind(0, 10, &dirty, e);
        statLimits.bind(0, 11, &dirty, e);
    }
    void wipe() { dirty = 0; statNames.wipe(); statLevels.wipe(); statLimits.wipe(); }
    void setUnusedClientId(uint32_t v) { if (v == unusedClientId) return; FG_DIRTY(0); unusedClientId = v; }
    void setFlags(uint32_t v) { if (v == flags) return; FG_DIRTY(1); flags = v; }
    void setPlayer(Entity* v) { if (v == player) return; FG_DIRTY(2); player = v; }
    void setFOV(double v) { if (v == FOV) return; FG_DIRTY(3); FOV = v; }
    void setLevel(double v) { if (v == level) return; FG_DIRTY(4); level = v; }
    void setTank(int v) { if (v == tank) return; FG_DIRTY(5); tank = v; }
    void setLevelbarProgress(double v) { if (v == levelbarProgress) return; FG_DIRTY(6); levelbarProgress = v; }
    void setLevelbarMax(double v) { if (v == levelbarMax) return; FG_DIRTY(7); levelbarMax = v; }
    void setStatsAvailable(double v) { if (v == statsAvailable) return; FG_DIRTY(8); statsAvailable = v; }
    void setCameraX(double v) { if (v == cameraX) return; FG_DIRTY(12); cameraX = v; }
    void setCameraY(double v) { if (v == cameraY) return; FG_DIRTY(13); cameraY = v; }
    void setScore(double v) { if (v == score) return; FG_DIRTY(14); score = v; }
    void setRespawnLevel(double v) { if (v == respawnLevel) return; FG_DIRTY(15); respawnLevel = v; }
    void setKilledBy(const std::string& v) { if (v == killedBy) return; FG_DIRTY(16); killedBy = v; }
    void setSpawnTick(double v) { if (v == spawnTick) return; FG_DIRTY(17); spawnTick = v; }
    void setDeathTick(double v) { if (v == deathTick) return; FG_DIRTY(18); deathTick = v; }
    void setTankOverride(const std::string& v) { if (v == tankOverride) return; FG_DIRTY(19); tankOverride = v; }
    void setMovementSpeed(double v) { if (v == movementSpeed) return; FG_DIRTY(20); movementSpeed = v; }
};

struct PositionData {
    uint32_t dirty = 0;
    Entity* entity;
    double x = 0;
    double y = 0;
    double angle = 0;
    uint32_t flags = 0;

    explicit PositionData(Entity* e) : entity(e) {}
    void wipe() { dirty = 0; }
    void setX(double v) { if (v == x) return; FG_DIRTY(0); x = v; }
    void setY(double v) { if (v == y) return; FG_DIRTY(1); y = v; }
    void setAngle(double v) { if (v == angle) return; FG_DIRTY(2); angle = v; }
    void setFlags(uint32_t v) { if (v == flags) return; FG_DIRTY(3); flags = v; }
};

struct StyleData {
    uint32_t dirty = 0;
    Entity* entity;
    uint32_t flags = 1;
    int color = Color::Border;
    double borderWidth = 7.5;
    double opacity = 1;
    double zIndex = 0;

    explicit StyleData(Entity* e) : entity(e) {}
    void wipe() { dirty = 0; }
    void setFlags(uint32_t v) { if (v == flags) return; FG_DIRTY(0); flags = v; }
    void setColor(int v) { if (v == color) return; FG_DIRTY(1); color = v; }
    void setBorderWidth(double v) { if (v == borderWidth) return; FG_DIRTY(2); borderWidth = v; }
    void setOpacity(double v) { if (v == opacity) return; FG_DIRTY(3); opacity = v; }
    void setZIndex(double v) { if (v == zIndex) return; FG_DIRTY(4); zIndex = v; }
};

struct ScoreData {
    uint32_t dirty = 0;
    Entity* entity;
    double score = 0;

    explicit ScoreData(Entity* e) : entity(e) {}
    void wipe() { dirty = 0; }
    void setScore(double v) { if (v == score) return; FG_DIRTY(0); score = v; }
};

struct TeamData {
    uint32_t dirty = 0;
    Entity* entity;
    int teamColor = Color::Border;
    double mothershipX = 0;
    double mothershipY = 0;
    uint32_t flags = 0;

    explicit TeamData(Entity* e) : entity(e) {}
    void wipe() { dirty = 0; }
    void setTeamColor(int v) { if (v == teamColor) return; FG_DIRTY(0); teamColor = v; }
    void setMothershipX(double v) { if (v == mothershipX) return; FG_DIRTY(1); mothershipX = v; }
    void setMothershipY(double v) { if (v == mothershipY) return; FG_DIRTY(2); mothershipY = v; }
    void setFlags(uint32_t v) { if (v == flags) return; FG_DIRTY(3); flags = v; }
};

#undef FG_DIRTY
