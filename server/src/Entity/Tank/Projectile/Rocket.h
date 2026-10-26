#pragma once
#include "Entity/Tank/Projectile/Bullet.h"

class Rocket : public Bullet {
public:
    Barrel* rocketBarrel;

    Rocket(Barrel* barrel, BarrelBase* tank, TankDefinition* tankDefinition, double shootAngle);

    void tick(int tick) override;
};
