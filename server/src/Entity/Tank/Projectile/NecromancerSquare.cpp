#include "Entity/Tank/Projectile/NecromancerSquare.h"
#include "Entity/Tank/Barrel.h"
#include "Entity/AI.h"
#include "Const/Enums.h"

NecromancerSquare::NecromancerSquare(Barrel* barrel, BarrelBase* tank, TankDefinition* tankDefinition, double shootAngle)
    : Drone(barrel, tank, tankDefinition, shootAngle)
{
    const BulletDefinition& bulletDef = barrel->definition->bullet;

    delete ai;
    ai = new AI(this);
    ai->viewRange = 600;
    ai->movementSpeed = baseAccel;
    ai->aimSpeed = baseAccel * 9;
    ai->doAimPrediction = true;
    ai->targetFilter = [this](const VectorAbstract& targetPos) -> bool {
        BarrelBase* t = this->tank.get();
        if (!t) return false;
        double dx = targetPos.x - t->positionData.x;
        double dy = targetPos.y - t->positionData.y;
        return dx * dx + dy * dy <= 1000.0 * 1000.0;
    };

    physicsData.setSides(4);

    int teamColor = Color::NecromancerSquare;
    if (tank->relationsData.team) {
        TeamData* td = tank->relationsData.team->getTeamData();
        if (td && td->teamColor) teamColor = td->teamColor;
    }
    styleData.setColor(teamColor);

    if (physicsData.flags & PhysicsFlags::noOwnTeamCollision)
        physicsData.setFlags(physicsData.flags ^ PhysicsFlags::noOwnTeamCollision);
    physicsData.setFlags(physicsData.flags | PhysicsFlags::onlySameOwnerCollision);

    minDamageMultiplier = 1;
    maxDamageMultiplier = 4;

    physicsData.setPushFactor(4);
    physicsData.setAbsorbtionFactor(bulletDef.absorbtionFactor);

    baseSpeed = 0;
    restingAccelDivisor = 18;
}

NecromancerSquare* NecromancerSquare::fromShape(Barrel* barrel, BarrelBase* tank, TankDefinition* tankDefinition, LivingEntity* shape) {
    NecromancerSquare* sunchip = new NecromancerSquare(barrel, tank, tankDefinition, shape->positionData.angle);

    sunchip->physicsData.setSides(shape->physicsData.sides);
    sunchip->physicsData.setSize(shape->physicsData.size);
    sunchip->positionData.setX(shape->positionData.x);
    sunchip->positionData.setY(shape->positionData.y);
    sunchip->positionData.setAngle(shape->positionData.angle);

    double shapeDamagePerTick = shape->damagePerTick;
    sunchip->damagePerTick *= shapeDamagePerTick / 2;
    double newHealth = sunchip->healthData.health * (shapeDamagePerTick / 2);
    sunchip->healthData.setHealth(newHealth);
    sunchip->healthData.setMaxHealth(newHealth);

    sunchip->scoreReward = shape->scoreReward;

    return sunchip;
}
