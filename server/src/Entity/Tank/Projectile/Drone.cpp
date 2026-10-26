#include "Entity/Tank/Projectile/Drone.h"
#include "Entity/Tank/Barrel.h"
#include "Native/Entity.h"
#include "Const/Enums.h"
#include "Util.h"

#include <cmath>

Drone::Drone(Barrel* barrel, BarrelBase* tank, TankDefinition* tankDefinition, double shootAngle)
    : Bullet(barrel, tank, tankDefinition, shootAngle)
{
    usePosAngle = true;

    ai = new AI(this);
    ai->viewRange = 900;
    ai->doAimPrediction = true;
    ai->targetFilter = [this](const VectorAbstract& targetPos) -> bool {
        BarrelBase* t = this->tank.get();
        if (!t) return false;
        double dx = targetPos.x - t->positionData.x;
        double dy = targetPos.y - t->positionData.y;
        return dx * dx + dy * dy <= 1500.0 * 1500.0;
    };

    canControlDrones = barrel->definition->canControlDrones.value_or(false);

    physicsData.setSides(3);
    if (physicsData.flags & PhysicsFlags::noOwnTeamCollision)
        physicsData.setFlags(physicsData.flags ^ PhysicsFlags::noOwnTeamCollision);
    physicsData.setFlags(physicsData.flags | PhysicsFlags::onlySameOwnerCollision);
    physicsData.setFlags(physicsData.flags ^ PhysicsFlags::canEscapeArena);
    styleData.setFlags(styleData.flags & ~StyleFlags::hasNoDmgIndicator);

    if (barrel->definition->bullet.lifeLength != -1) {
        lifeLength = 88 * barrel->definition->bullet.lifeLength;
    } else {
        lifeLength = 1e300; // Infinity
    }
    deathAccelFactor = 1;

    physicsData.setPushFactor(4);
    physicsData.setAbsorbtionFactor(barrel->definition->bullet.absorbtionFactor);

    baseSpeed /= 3;

    barrel->droneCount += 1;

    ai->movementSpeed = baseAccel;
    ai->aimSpeed = baseAccel * 9;

    minDamageMultiplier = 1;
    maxDamageMultiplier = 1;
}

void Drone::destroy(bool animate) {
    if (!animate && Entity::exists(barrelEntity)) barrelEntity->droneCount -= 1;
    Bullet::destroy(animate);
}

void Drone::tickMixin(int tick) {
    Bullet::tick(tick);
}

void Drone::restingBehavior(BarrelBase* owner) {
    double dx = positionData.x - owner->positionData.x;
    double dy = positionData.y - owner->positionData.y;
    double distSq = dx * dx + dy * dy;

    double returnThresh = owner->physicsData.size * 1.2;
    if (restReturning) {
        if (distSq < returnThresh * returnThresh) restReturning = false;
    } else {
        if (distSq > MAX_RESTING_RADIUS) restReturning = true;
    }

    if (restReturning) {
        positionData.setAngle(std::atan2(-dy, -dx));
    } else {
        baseAccel /= restingAccelDivisor;
        positionData.setAngle(std::atan2(dy, dx) + M_PI / 2);
    }
}

void Drone::tick(int tick) {
    BarrelBase* owner = tank.get();
    bool usingAI = !canControlDrones
        || !owner || owner->inputs->deleted
        || (!owner->inputs->attemptingShot() && !owner->inputs->attemptingRepel());
    Inputs* inputs = ((!usingAI && owner) ? owner->inputs : ai->inputs).get();

    if (usingAI && ai->state == AIState::idle && owner) {
        double base = baseAccel;
        restingBehavior(owner);

        if (!Entity::exists(barrelEntity)) destroy(true);

        tickMixin(tick);

        baseAccel = base;
        return;
    } else {
        positionData.setAngle(std::atan2(
            inputs->mouse.y - positionData.y,
            inputs->mouse.x - positionData.x));
        restReturning = false;
    }

    if (canControlDrones && inputs->attemptingRepel()) {
        positionData.setAngle(positionData.angle + M_PI);
    }

    if (!Entity::exists(barrelEntity)) destroy(true);

    tickMixin(tick);
}
