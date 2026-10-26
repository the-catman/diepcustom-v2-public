#include "Entity/Tank/Projectile/Minion.h"
#include "Entity/Tank/Barrel.h"
#include "Entity/AI.h"
#include "Const/Enums.h"
#include "Physics/Vector.h"

#include <cmath>

static BarrelDefinition MinionBarrelDefinition = {
    /* angle */ 0,
    /* offset */ 0,
    /* distance */ std::nullopt,
    /* size */ 85,
    /* width */ 50.4,
    /* delay */ 0,
    /* reload */ 1,
    /* recoil */ 1,
    /* isTrapezoid */ false,
    /* trapezoidDirection */ 0,
    /* addon */ "",
    /* droneCount */ std::nullopt,
    /* canControlDrones */ std::nullopt,
    /* forceFire */ std::nullopt,
    /* color */ std::nullopt,
    /* bullet */ {
        /* type */ "bullet",
        /* sizeRatio */ 1,
        /* health */ 0.4,
        /* damage */ 0.4,
        /* speed */ 0.8,
        /* scatterRate */ 1,
        /* lifeLength */ 1,
        /* absorbtionFactor */ 1,
        /* color */ std::nullopt,
        /* sides */ std::nullopt,
    }
};

Minion::Minion(Barrel* barrel, BarrelBase* tank, TankDefinition* tankDefinition, double shootAngle)
    : Drone(barrel, tank, tankDefinition, shootAngle)
{
    inputs = ai->inputs;
    ai->viewRange = 900;
    usePosAngle = false;

    physicsData.setSides(1);
    physicsData.setSize(physicsData.size * 1.2);
    scaleFactor = physicsData.size / 50;

    if (physicsData.flags & PhysicsFlags::noOwnTeamCollision)
        physicsData.setFlags(physicsData.flags ^ PhysicsFlags::noOwnTeamCollision);
    if (physicsData.flags & PhysicsFlags::canEscapeArena)
        physicsData.setFlags(physicsData.flags ^ PhysicsFlags::canEscapeArena);

    physicsData.setFlags(physicsData.flags | PhysicsFlags::onlySameOwnerCollision);

    cameraEntity = tank->cameraEntity;
    reloadTime = 1;

    new Barrel(this, &MinionBarrelDefinition);
    ai->movementSpeed = ai->aimSpeed = baseAccel;

    arenaMobID = "factoryDrone";
}

void Minion::tickMixin(int tick) {
    BarrelBase* owner = tank.get();
    if (!owner) { Drone::tickMixin(tick); return; }
    reloadTime = owner->reloadTime;

    bool usingAI = !canControlDrones
        || (!owner->inputs->attemptingShot() && !owner->inputs->attemptingRepel());
    Inputs* inp = (!usingAI ? owner->inputs : ai->inputs).get();

    if (usingAI && ai->state == AIState::idle) {
        movementAngle = positionData.angle;
    } else {
        inputs->flags |= InputFlags::leftclick;

        double dist = inp->mouse.distanceToSQ(VectorAbstract{positionData.x, positionData.y});

        if (dist < FOCUS_RADIUS / 7) {
            movementAngle = positionData.angle + M_PI;
        } else if (dist < FOCUS_RADIUS) {
            movementAngle = positionData.angle + M_PI / 2;
        } else {
            movementAngle = positionData.angle;
        }
    }

    Drone::tickMixin(tick);
}
