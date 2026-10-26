#pragma once
#include "Entity/Tank/Projectile/Drone.h"

class NecromancerSquare : public Drone {
public:
    NecromancerSquare(Barrel* barrel, BarrelBase* tank, TankDefinition* tankDefinition, double shootAngle);

    static NecromancerSquare* fromShape(Barrel* barrel, BarrelBase* tank, TankDefinition* tankDefinition, LivingEntity* shape);
};
