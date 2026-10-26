#pragma once
#include "Entity/Tank/Projectile/Bullet.h"

class Trap : public Bullet {
public:
    Trap(Barrel* barrel, BarrelBase* tank, TankDefinition* tankDefinition, double shootAngle);

    void tick(int tick) override;

protected:
    int collisionEnd = 0;
};
