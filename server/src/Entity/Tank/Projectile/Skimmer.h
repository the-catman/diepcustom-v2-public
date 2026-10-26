#pragma once
#include "Entity/Tank/Projectile/Bullet.h"

class Skimmer : public Bullet {
public:
    static constexpr double BASE_ROTATION = 0.1;

    Skimmer(Barrel* barrel, BarrelBase* tank, TankDefinition* tankDefinition, double shootAngle, double direction);

    void tick(int tick) override;

private:
    double rotationPerTick;
    BarrelDefinition s1Def, s2Def;
    Barrel* s1;
    Barrel* s2;
};
