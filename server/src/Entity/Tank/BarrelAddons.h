#pragma once

#include <string>
#include <unordered_map>
#include <functional>

#include "Entity/Object.h"

class Barrel;
class GameServer;

class BarrelAddon {
public:
    explicit BarrelAddon(Barrel* owner);
    virtual ~BarrelAddon() = default;

protected:
    GameServer* game;
    Barrel* owner;
};

class TrapLauncher : public ObjectEntity {
public:
    Barrel* barrelEntity;

    explicit TrapLauncher(Barrel* barrel);

    void resize();
    void tick(int tick) override;
};

class TrapLauncherAddon : public BarrelAddon {
public:
    TrapLauncher* launcherEntity;

    explicit TrapLauncherAddon(Barrel* owner);
};

class PurpleBarrelAddon : public BarrelAddon {
public:
    explicit PurpleBarrelAddon(Barrel* owner);
};

using BarrelAddonFactory = std::function<BarrelAddon*(Barrel*)>;
extern const std::unordered_map<std::string, BarrelAddonFactory> BarrelAddonById;
