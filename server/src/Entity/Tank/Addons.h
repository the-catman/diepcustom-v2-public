#pragma once

#include <string>
#include <unordered_map>
#include <functional>

#include "Entity/Object.h"
#include "Entity/Live.h"
#include "Entity/AI.h"
#include "Const/TankDefinitions.h"

class GameServer;
class AutoTurret;

class Addon {
public:
    explicit Addon(BarrelBase* owner);
    virtual ~Addon() = default;

protected:
    GameServer* game;
    BarrelBase* owner;

    class GuardObject* createGuard(double sides, double sizeRatio, double offsetAngle, double radiansPerTick);
    void createAutoTurrets(int count);
};

class GuardObject : public ObjectEntity {
public:
    double radiansPerTick;

    GuardObject(GameServer* game, BarrelBase* owner, double sides, double scaleFactor, double offsetAngle, double radiansPerTick);

    void onKill(LivingEntity* killedEntity, LivingEntity* weapon) override;
    void tick(int tick) override;

protected:
    BarrelBase* guardOwner;
};

using AddonFactory = std::function<Addon*(BarrelBase*)>;
extern const std::unordered_map<std::string, AddonFactory> AddonById;
