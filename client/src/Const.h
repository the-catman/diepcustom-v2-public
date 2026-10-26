#pragma once

#include <cstdint>
#include <string>

namespace config {
    const std::string BUILD_HASH = "6f59094d60f98fafc14371671d3ff31ef4d75d9e";
    extern const uint32_t MAGIC_NUM;
    extern double uiScale;
    extern double gridBaseAlpha;
    extern uint32_t gridColor;
    extern uint32_t backgroundColor;
    extern uint32_t borderColor;
    extern double borderAlpha;
    extern double strokeIntensity;
    extern uint32_t healthBackgroundColor;
    extern uint32_t healthFillColor;
    extern uint32_t xpBarFillColor;
    extern uint32_t scoreBarFillColor;
    extern uint32_t minimapBackgroundColor;
    extern uint32_t minimapBorderColor;
    extern uint32_t strokeSolidColor;
    extern uint32_t barBackgroundColor;
    extern bool renUi;
    extern bool renBackground;
    extern bool renSolidBackground;
    extern bool renCacheGrid;
    extern bool renNames;
    extern bool renScoreboard;
    extern bool renScoreboardNames;
    extern bool renHealthBars;
    extern bool renAchievements;
    extern bool renUpgrades;
    extern bool renChangelog;
    extern bool renStrokeSoftColor;
    extern bool renRawHealthValues;
    extern bool renFps;
    extern bool renDebugInfo;
    extern bool renDebugCollisions;
    extern bool renContextReinitialization;
    extern bool renMinimapViewport;
    extern bool renPatternGrid;
    extern bool renStats;
    extern bool netPredictMovement;
    extern bool netForceSecure;
    extern int netLatencyHistorySize;
    extern bool uiPreventRightClick;
    // Stat queue: digit chars ('8' - statIndex), drained by the stat panel walk.
    extern std::string gameStatsBuild;
    // WebGL2 world renderer; off (the Canvas2D world path) without WebGL2 or with ?canvas2d.
    extern bool renWorldGL;
}

namespace ServerBound {
    constexpr uint8_t Init = 0x0;
    constexpr uint8_t Input = 0x1;
    constexpr uint8_t Spawn = 0x2;
    constexpr uint8_t StatUpgrade = 0x3;
    constexpr uint8_t TankUpgrade = 0x4;
    constexpr uint8_t Ping = 0x5;
    constexpr uint8_t TCPInit = 0x6;
    constexpr uint8_t ExtensionFound = 0x7;
    constexpr uint8_t ToRespawn = 0x8;
    constexpr uint8_t TakeTank = 0x9;
}

namespace ClientBound {
    constexpr uint8_t Update = 0x0;
    constexpr uint8_t OutdatedClient = 0x1;
    constexpr uint8_t Compressed = 0x2;
    constexpr uint8_t Notification = 0x3;
    constexpr uint8_t ServerInfo = 0x4;
    constexpr uint8_t Ping = 0x5;
    constexpr uint8_t PartyCode = 0x6;
    constexpr uint8_t Accept = 0x7;
    constexpr uint8_t Achievement = 0x8;
    constexpr uint8_t InvalidParty = 0x9;
    constexpr uint8_t PlayerCount = 0xa;
    constexpr uint8_t ProofOfWork = 0xb;
}

namespace InputFlags {
    constexpr uint32_t leftclick = 1 << 0;
    constexpr uint32_t up = 1 << 1;
    constexpr uint32_t left = 1 << 2;
    constexpr uint32_t down = 1 << 3;
    constexpr uint32_t right = 1 << 4;
    constexpr uint32_t invincibility = 1 << 5;
    constexpr uint32_t suicide = 1 << 6;
    constexpr uint32_t rightclick = 1 << 7;
    constexpr uint32_t levelup = 1 << 8;
    constexpr uint32_t gamepad = 1 << 9;
    constexpr uint32_t switchtank = 1 << 10;
    constexpr uint32_t adblock = 1 << 11;
}

namespace ArenaFlags {
    constexpr uint32_t noJoining = 1 << 0;
    constexpr uint32_t showsLeaderArrow = 1 << 1;
    constexpr uint32_t hiddenScores = 1 << 2;
    constexpr uint32_t gameReadyStart = 1 << 3;
    constexpr uint32_t canUseCheats = 1 << 4;
}

namespace TeamFlags {
    constexpr uint32_t hasMothership = 1 << 0;
}

namespace CameraFlags {
    constexpr uint32_t usesCameraCoords = 1 << 0;
    constexpr uint32_t showingDeathStats = 1 << 1;
    constexpr uint32_t gameWaitingStart = 1 << 2;
}

namespace StyleFlags {
    constexpr uint32_t isVisible = 1 << 0;
    constexpr uint32_t hasBeenDamaged = 1 << 1;
    constexpr uint32_t isFlashing = 1 << 2;
    constexpr uint32_t renderFirst = 1 << 3;
    constexpr uint32_t isStar = 1 << 4;
    constexpr uint32_t isCachable = 1 << 5;
    constexpr uint32_t showsAboveParent = 1 << 6;
    constexpr uint32_t hasNoDmgIndicator = 1 << 7;
}

namespace PositionFlags {
    constexpr uint32_t absoluteRotation = 1 << 0;
    constexpr uint32_t canMoveThroughWalls = 1 << 1;
}

namespace PhysicsFlags {
    constexpr uint32_t isTrapezoid = 1 << 0;
    constexpr uint32_t showsOnMap = 1 << 1;
    constexpr uint32_t doChildrenCollision = 1 << 2;
    constexpr uint32_t noOwnTeamCollision = 1 << 3;
    constexpr uint32_t isSolidWall = 1 << 4;
    constexpr uint32_t onlySameOwnerCollision = 1 << 5;
    constexpr uint32_t isBase = 1 << 6;
    constexpr uint32_t _unknown1 = 1 << 7;
    constexpr uint32_t canEscapeArena = 1 << 8;
}

namespace HealthFlags {
    constexpr uint32_t hiddenHealthbar = 1 << 0;
}

namespace NameFlags {
    constexpr uint32_t hiddenName = 1 << 0;
    constexpr uint32_t highlightedName = 1 << 1;
}

namespace Color {
    constexpr int Border = 0;
    constexpr int Barrel = 1;
    constexpr int Tank = 2;
    constexpr int TeamBlue = 3;
    constexpr int TeamRed = 4;
    constexpr int TeamPurple = 5;
    constexpr int TeamGreen = 6;
    constexpr int Shiny = 7;
    constexpr int EnemySquare = 8;
    constexpr int EnemyTriangle = 9;
    constexpr int EnemyPentagon = 10;
    constexpr int EnemyCrasher = 11;
    constexpr int Neutral = 12;
    constexpr int ScoreboardBar = 13;
    constexpr int Box = 14;
    constexpr int EnemyTank = 15;
    constexpr int NecromancerSquare = 16;
    constexpr int Fallen = 17;
}

constexpr int NET_COLOR_COUNT = 19;
constexpr int UI_COLOR_COUNT = 8;
extern uint32_t NET_COLORS[NET_COLOR_COUNT];
extern uint32_t UI_COLORS[UI_COLOR_COUNT];

uint32_t colorHex(int color);
uint32_t teamColorHex(int index);
uint32_t computeMagicNum(const std::string& build);
