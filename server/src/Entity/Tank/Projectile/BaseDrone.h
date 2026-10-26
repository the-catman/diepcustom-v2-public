#pragma once
#include "Entity/Tank/Projectile/Drone.h"

class BaseDrone : public Drone {
public:
    BaseDrone(Barrel* barrel, BarrelBase* tank, TankDefinition* tankDefinition, double shootAngle);
};
