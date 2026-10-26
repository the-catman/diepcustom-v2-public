#include "Entity/Tank/Projectile/Swarm.h"
#include "Entity/Tank/Barrel.h"
#include "Const/Enums.h"
#include "Util.h"

#include <algorithm>
#include <cmath>

Swarm::Swarm(Barrel* barrel, BarrelBase* tank, TankDefinition* tankDefinition, double shootAngle)
    : Drone(barrel, tank, tankDefinition, shootAngle)
{
    ai->viewRange = 2000;
    ai->targetFilter = [](const VectorAbstract&) { return true; };
    physicsData.setFlags(physicsData.flags | PhysicsFlags::canEscapeArena | PhysicsFlags::noOwnTeamCollision);
    physicsData.setPushFactor(1.2);

    if (barrel->definition->bullet.lifeLength != -1) {
        lifeLength = 88 * barrel->definition->bullet.lifeLength;
        // I'm not sure if it's 104 or 88. I'm going with 88
        // since I'm pretty sure the buff was after M28.
    }
}

// No target -> full-speed wander: straight runs of 20-60 ticks, then a sweeping turn.
void Swarm::restingBehavior(BarrelBase* owner) {
    (void)owner;
    if (!wandering || positionData.angle != wanderHeading) {
        wandering = true;
        wanderHeading = wanderTarget = positionData.angle;
        wanderHold = 20 + (int)(util::randomFloat() * 40);
    }

    double diff = std::remainder(wanderTarget - wanderHeading, 2 * M_PI);
    if (diff != 0) {
        wanderHeading += std::clamp(diff, -0.25, 0.25);
    } else if (--wanderHold <= 0) {
        wanderTarget = util::randomFloat() * 2 * M_PI;
        wanderHold = 20 + (int)(util::randomFloat() * 40);
    }

    positionData.setAngle(wanderHeading);
}
