#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "sim/Lerp.h"

struct EntityId {
    uint32_t hash = 0;
    uint32_t id = 0;

    explicit operator bool() const { return hash != 0; }
};

struct RelationsData {
    EntityId parent;
    EntityId owner;
    EntityId team;
};

struct BarrelData {
    uint32_t flags = 0;
    double reloadTime = 15;
    double trapezoidDirection = 0;
    double lastFiredTime = -1e18;
};

struct PhysicsData {
    uint32_t flags = 0;
    double sides = 0;
    LerpVal size;
    LerpVal width;
    double absorbtionFactor = 1;
    double pushFactor = 8;
};

struct HealthData {
    uint32_t flags = 0;
    LerpVal health;
    LerpVal maxHealth;
    float barFade = 0;
    HealthData() { health.snap(1); maxHealth.snap(1); }
};

struct ArenaData {
    uint32_t flags = 2;
    double leftX = 0;
    double topY = 0;
    double rightX = 0;
    double bottomY = 0;
    double scoreboardAmount = 0;
    std::string scoreboardNames[10];
    double scoreboardScores[10] = {};
    int scoreboardColors[10] = {};
    std::string scoreboardSuffixes[10];
    int scoreboardTanks[10] = {};
    double leaderX = 0;
    double leaderY = 0;
    double playersNeeded = 1;
    LerpVal ticksUntilStart;
    ArenaData() { ticksUntilStart.snap(250); }
};

struct NameData {
    uint32_t flags = 0;
    std::string name;
    float fade = 0;
};

struct CameraData {
    uint32_t unusedClientId = 0;
    uint32_t flags = 1;
    EntityId player;
    LerpVal FOV;
    double level = 1;
    int tank = 53;
    double levelbarProgress = 0;
    double levelbarMax = 0;
    // Every levelbarMax after the first (a level change) plays the level bar's fill; the
    // renderer clears it.
    bool levelbarMaxSeen = false;
    bool levelbarFill = false;
    double statsAvailable = 0;
    std::string statNames[8];
    double statLevels[8] = {};
    double statLimits[8] = {};
    LerpVal cameraX;
    LerpVal cameraY;
    LerpVal score;
    double respawnLevel = 0;
    std::string killedBy;
    double spawnTick = 0;
    double deathTick = -1;
    std::string tankOverride;
    double movementSpeed = 0;
    CameraData() { FOV.snap(0.35f); }
};

struct PositionData {
    LerpVal x;
    LerpVal y;
    LerpVal angle;
    uint32_t flags = 0;
    // Client-side offset: barrel recoil.
    float offX = 0;
    float offY = 0;
};

struct StyleData {
    uint32_t flags = 1;
    int color = 0;
    LerpVal borderWidth;
    LerpVal opacity;
    double zIndex = 0;
    double flagsTime = -1e18;
    StyleData() { borderWidth.snap(7.5f); opacity.snap(1); }
};

struct ScoreData {
    LerpVal score;
};

struct TeamData {
    int teamColor = 0;
    double mothershipX = 0;
    double mothershipY = 0;
    uint32_t flags = 0;
};

struct Entity {
    uint32_t id = 0;
    uint32_t hash = 0;
    double createdTime = -1e18;
    double lastDamageTime = -1e18;

    std::optional<RelationsData> relations;
    std::optional<BarrelData> barrel;
    std::optional<PhysicsData> physics;
    std::optional<HealthData> health;
    std::optional<ArenaData> arena;
    std::optional<NameData> name;
    std::optional<CameraData> camera;
    std::optional<PositionData> position;
    std::optional<StyleData> style;
    std::optional<ScoreData> score;
    std::optional<TeamData> team;
};
