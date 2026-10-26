#pragma once
#include "Entity/Tank/Projectile/Bullet.h"

class CrocSkimmer : public Bullet {
public:
    CrocSkimmer(Barrel* barrel, BarrelBase* tank, TankDefinition* tankDefinition, double shootAngle);

    void tick(int tick) override;

private:
    BarrelDefinition s1Def, s2Def;
};
