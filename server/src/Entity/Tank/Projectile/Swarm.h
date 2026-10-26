#pragma once
#include "Entity/Tank/Projectile/Drone.h"

class Swarm : public Drone {
public:
    Swarm(Barrel* barrel, BarrelBase* tank, TankDefinition* tankDefinition, double shootAngle);

protected:
    void restingBehavior(BarrelBase* owner) override;

private:
    double wanderHeading;
    double wanderTarget;
    int wanderHold = 0;
    bool wandering = false;
};
