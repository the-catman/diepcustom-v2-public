#include "Entity/Tank/Projectile/Trap.h"
#include "Entity/Tank/Barrel.h"
#include "Const/Enums.h"
#include "Const/TankDefinitions.h"
#include "Util.h"

#include <cmath>

Trap::Trap(Barrel* barrel, BarrelBase* tank, TankDefinition* tankDefinition, double shootAngle)
    : Bullet(barrel, tank, tankDefinition, shootAngle)
{
    baseSpeed = (barrel->bulletAccel / 2) + 30 - util::randomFloat() * barrel->definition->bullet.scatterRate;
    baseAccel = 0;

    physicsData.setSides(3);
    if (physicsData.flags & PhysicsFlags::noOwnTeamCollision)
        physicsData.setFlags(physicsData.flags ^ PhysicsFlags::noOwnTeamCollision);
    physicsData.setFlags(physicsData.flags | PhysicsFlags::onlySameOwnerCollision);

    styleData.setFlags(styleData.flags | StyleFlags::isStar | StyleFlags::isCachable);
    styleData.setFlags(styleData.flags & ~StyleFlags::hasNoDmgIndicator);

    collisionEnd = (int)lifeLength >> 3;
    lifeLength = (int)(600 * barrel->definition->bullet.lifeLength) >> 3;

    positionData.setAngle(util::randomFloat() * util::PI2);
}

void Trap::tick(int tick) {
    Bullet::tick(tick);

    if (tick - spawnTick == collisionEnd) {
        if (physicsData.flags & PhysicsFlags::onlySameOwnerCollision)
            physicsData.setFlags(physicsData.flags ^ PhysicsFlags::onlySameOwnerCollision);
        physicsData.setFlags(physicsData.flags | PhysicsFlags::noOwnTeamCollision);
    }
}
