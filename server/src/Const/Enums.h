#pragma once

#include <cstdint>
#include <vector>

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
    constexpr int kMaxColors = 18;
}

uint32_t colorHex(int color);

namespace Tank {
    constexpr int Basic = 0;
    constexpr int Twin = 1;
    constexpr int Triplet = 2;
    constexpr int TripleShot = 3;
    constexpr int QuadTank = 4;
    constexpr int OctoTank = 5;
    constexpr int Sniper = 6;
    constexpr int MachineGun = 7;
    constexpr int FlankGuard = 8;
    constexpr int TriAngle = 9;
    constexpr int Destroyer = 10;
    constexpr int Overseer = 11;
    constexpr int Overlord = 12;
    constexpr int TwinFlank = 13;
    constexpr int PentaShot = 14;
    constexpr int Assassin = 15;
    constexpr int ArenaCloser = 16;
    constexpr int Necromancer = 17;
    constexpr int TripleTwin = 18;
    constexpr int Hunter = 19;
    constexpr int Gunner = 20;
    constexpr int Stalker = 21;
    constexpr int Ranger = 22;
    constexpr int Booster = 23;
    constexpr int Fighter = 24;
    constexpr int Hybrid = 25;
    constexpr int Manager = 26;
    constexpr int Mothership = 27;
    constexpr int Predator = 28;
    constexpr int Sprayer = 29;
    constexpr int Trapper = 31;
    constexpr int GunnerTrapper = 32;
    constexpr int Overtrapper = 33;
    constexpr int MegaTrapper = 34;
    constexpr int TriTrapper = 35;
    constexpr int Smasher = 36;
    constexpr int Landmine = 38;
    constexpr int AutoGunner = 39;
    constexpr int Auto5 = 40;
    constexpr int Auto3 = 41;
    constexpr int SpreadShot = 42;
    constexpr int Streamliner = 43;
    constexpr int AutoTrapper = 44;
    constexpr int DominatorD = 45;
    constexpr int DominatorG = 46;
    constexpr int DominatorT = 47;
    constexpr int Battleship = 48;
    constexpr int Annihilator = 49;
    constexpr int AutoSmasher = 50;
    constexpr int Spike = 51;
    constexpr int Factory = 52;
    constexpr int MountedTurret = 53;
    constexpr int Skimmer = 54;
    constexpr int Rocketeer = 55;
}

namespace Stat {
    constexpr int MovementSpeed = 0;
    constexpr int Reload = 1;
    constexpr int BulletDamage = 2;
    constexpr int BulletPenetration = 3;
    constexpr int BulletSpeed = 4;
    constexpr int BodyDamage = 5;
    constexpr int MaxHealth = 6;
    constexpr int HealthRegen = 7;
}
constexpr int StatCount = 8;

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

namespace BarrelFlags {
    constexpr uint32_t hasShot = 1 << 0;
}

namespace HealthFlags {
    constexpr uint32_t hiddenHealthbar = 1 << 0;
}

namespace NameFlags {
    constexpr uint32_t hiddenName = 1 << 0;
    constexpr uint32_t highlightedName = 1 << 1;
}

namespace EntityTags {
    constexpr uint32_t isShape = 1 << 0;
    constexpr uint32_t isTank = 1 << 1;
    constexpr uint32_t isDominator = 1 << 2;
    constexpr uint32_t isBoss = 1 << 3;
    constexpr uint32_t isShiny = 1 << 4;
    constexpr uint32_t isBullet = 1 << 5;
    constexpr uint32_t isTeamGroup = 1 << 6;
}

// `[index: level]->score at level`. Built once at startup.
extern std::vector<double> levelToScoreTable;
double levelToScore(int level);
void initLevelToScoreTable();
