#pragma once

#include <cstdint>
#include <string>

namespace config {
    extern const std::string buildHash;

    extern int serverPort;
    extern std::string host;
    extern std::string mode;

    constexpr double mspt = 40;
    constexpr double tps = 1000.0 / mspt;
    constexpr int connectionsPerIp = 0x7fffffff; // Infinity (no practical limit)
    constexpr int wssMaxMessageSize = 4096;

    constexpr double countdownDuration = 0 * tps;
    constexpr double shinyChance = 1.0 / 1000000;
    constexpr double factorySpawnChance = 0.05;

    constexpr bool enableApi = true;
    extern const std::string apiLocation;
    constexpr bool enableCommands = true;
    constexpr bool enableClient = true;
    extern const std::string clientLocation;

    extern const uint32_t magicNum;

    constexpr double bossSpawningInterval = 45 * 60 * tps;
    constexpr double scoreboardUpdateInterval = 1 * tps;

    // sha256 hex of the dev password, from DEV_PASSWORD_HASH; unset, nobody gets dev access.
    extern std::string devPasswordHash;
    constexpr bool doVerboseLogs = false;

    namespace AccessLevel {
        constexpr int FullAccess = 3;
        constexpr int BetaAccess = 2;
        constexpr int kReserved = 1;
        constexpr int PublicAccess = 0;
        constexpr int NoAccess = -1;
    }
    constexpr int unbannableLevelMinimum = AccessLevel::FullAccess;
    constexpr int defaultAccessLevel = AccessLevel::PublicAccess;

    constexpr int maxPlayerLevel = 45;
    constexpr bool enableAchievements = true;

    void init();
}
