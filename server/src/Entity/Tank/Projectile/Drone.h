#pragma once
#include "Entity/Tank/Projectile/Bullet.h"
#include "Entity/AI.h"

class Drone : public Bullet {
public:
    std::unique_ptr<AI> ai;
    static constexpr double MAX_RESTING_RADIUS = 400.0 * 400.0;

    Drone(Barrel* barrel, BarrelBase* tank, TankDefinition* tankDefinition, double shootAngle);

    void destroy(bool animate = true) override;
    void tick(int tick) override;

protected:
    virtual void tickMixin(int tick);
    virtual void restingBehavior(BarrelBase* owner);
    bool canControlDrones = false;
    bool restReturning = false;
    bool restFacesVelocity = true;
    int restingAccelDivisor = 6;
};
