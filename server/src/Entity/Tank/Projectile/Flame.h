#pragma once
#include "Entity/Tank/Projectile/Bullet.h"

class Flame : public Bullet {
public:
    Flame(Barrel* barrel, BarrelBase* tank, TankDefinition* tankDefinition, double shootAngle);

    void destroy(bool animate = true) override;
    void tick(int tick) override;
};
