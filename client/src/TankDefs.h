#pragma once

#include <string>

namespace tankdefs {
    // Refetches /api/tanks, servers, commands and colors (on every connect).
    void fetch();
    // util_reload_<name> [?interval]; interval is null to reload once.
    void reload(const char* name, const char* interval);
    std::string name(int tankId);
    int count();
    // Bumped whenever the tank defs (re)arrive.
    int version();
    int levelRequirement(int tankId);
    int bodySides(int tankId);
    // Fills out[maxCount][8] with (angle, size, width/42, offset, isTrapezoid,
    // trapezoidDirection, distance, hasTrapLauncher).
    int barrels(int tankId, float* out, int maxCount);
    std::string preAddon(int tankId);
    std::string postAddon(int tankId);
    int upgrades(int tankId, int* out, int maxCount);
    int serverCount();
    // The server list arrived, or every fetch of it failed.
    bool serversSettled();
    // Party link server ids: id -> gamemode ("" when unknown) and gamemode -> id.
    std::string serverGamemodeById(const std::string& id);
    std::string serverId(const std::string& gamemode);
    std::string serverGamemode(int index);
    std::string serverName(int index);
    std::string gamemodeName(const std::string& id);
    int commandCount();
    std::string commandId(int index);
    std::string commandUsage(int index);
    std::string commandDescription(int index);
    // permissionLevel not above level.
    bool commandAllowed(int index, int level);
    // Colors from the last /api/colors reply as (id, color) pairs, each returned once.
    int takeColors(int* out, int maxCount);
}
