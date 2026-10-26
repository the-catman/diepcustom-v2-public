#include "Entity/Tank/Projectile/Flame.h"
#include "Entity/Tank/Barrel.h"
#include "Const/Enums.h"

Flame::Flame(Barrel* barrel, BarrelBase* tank, TankDefinition* tankDefinition, double shootAngle)
    : Bullet(barrel, tank, tankDefinition, shootAngle)
{
    baseSpeed *= 2;
    baseAccel = 0;
    damageReduction = 1;

    physicsData.setSides(4);
    physicsData.setAbsorbtionFactor(0);
    physicsData.setPushFactor(0);

    lifeLength = 25 * barrel->definition->bullet.lifeLength;
}

void Flame::destroy(bool animate) {
    Bullet::destroy(false);
}

void Flame::tick(int tick) {
    Bullet::tick(tick);

    damageReduction += 1.0 / 25;
    styleData.setOpacity(styleData.opacity - 1.0 / 25);
}
