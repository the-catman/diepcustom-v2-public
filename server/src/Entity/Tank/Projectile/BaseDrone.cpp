#include "Entity/Tank/Projectile/BaseDrone.h"
#include "Entity/Tank/Barrel.h"
#include "Entity/AI.h"

BaseDrone::BaseDrone(Barrel* barrel, BarrelBase* tank, TankDefinition* tankDefinition, double shootAngle)
    : Drone(barrel, tank, tankDefinition, shootAngle)
{
    restingAccelDivisor = 9;
    ai->viewRange = 3500;
    ai->targetArenaTeam = false;
    ai->targetFilter = [this](const VectorAbstract& targetPos) -> bool {
        BarrelBase* t = this->tank.get();
        if (!t) return false;
        double dx = targetPos.x - t->positionData.x;
        double dy = targetPos.y - t->positionData.y;
        return dx * dx + dy * dy <= 5000.0 * 5000.0;
    };
}
