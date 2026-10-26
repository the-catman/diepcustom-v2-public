#pragma once
#include "Entity/Tank/Projectile/Drone.h"

class Minion : public Drone {
public:
    static constexpr double FOCUS_RADIUS = 800.0 * 800;

    Minion(Barrel* barrel, BarrelBase* tank, TankDefinition* tankDefinition, double shootAngle);

protected:
    void tickMixin(int tick) override;
};
