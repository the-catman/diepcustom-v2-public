#include "Entity/Tank/AutoTurret.h"
#include "Entity/Tank/Barrel.h"
#include "Entity/Live.h"
#include "Util.h"
#include "Const/Enums.h"

#include <cmath>
#include <optional>

const BarrelDefinition AutoTurretDefinition = {
    .angle = 0,
    .offset = 0,
    .distance = std::nullopt,
    .size = 55,
    .width = 42 * 0.7,
    .delay = 0.01,
    .reload = 1,
    .recoil = 0.3,
    .isTrapezoid = false,
    .trapezoidDirection = 0,
    .addon = "",
    .droneCount = std::nullopt,
    .canControlDrones = std::nullopt,
    .forceFire = std::nullopt,
    .color = std::nullopt,
    .bullet = {
        .type = "bullet",
        .sizeRatio = 1,
        .health = 1,
        .damage = 0.3,
        .speed = 1.2,
        .scatterRate = 1,
        .lifeLength = 1,
        .absorbtionFactor = 1,
        .color = std::nullopt,
        .sides = std::nullopt
    }
};

AutoTurret::AutoTurret(BarrelBase* ownerIn, const BarrelDefinition* turretDefinition, double bSize)
    : ObjectEntity(ownerIn->game), nameData(this),
      owner(ownerIn), baseSize(bSize) {
    arenaMobID = "AutoTurret";
    cameraEntity = ownerIn->cameraEntity;
    reloadTime = 15;

    ai = new AI(this);
    ai->doAimPrediction = true;
    inputs = ai->inputs;

    setParent(ownerIn);
    relationsData.setOwner(ownerIn);
    relationsData.setTeam(ownerIn->relationsData.team);

    physicsData.setSides(1);
    physicsData.setSize(baseSize * ownerIn->rootParent->scaleFactor);
    scaleFactor = ownerIn->rootParent->scaleFactor;

    styleData.setColor(Color::Barrel);
    styleData.setFlags(styleData.flags | StyleFlags::showsAboveParent);

    positionData.setFlags(positionData.flags | PositionFlags::absoluteRotation);

    nameData.setName("Mounted Turret");
    nameData.setFlags(nameData.flags | NameFlags::hiddenName);

    turretDefinitionStorage = turretDefinition ? *turretDefinition : AutoTurretDefinition;
    turret = new Barrel(this, &turretDefinitionStorage);
    turret->physicsData.setFlags(turret->physicsData.flags | PhysicsFlags::doChildrenCollision);
}

void AutoTurret::onKill(LivingEntity* killedEntity, LivingEntity* weapon) {
    if (owner) owner->onKill(killedEntity, weapon);
}

void AutoTurret::tick(int tick) {
    if (inputs != ai->inputs) inputs = ai->inputs;

    relationsData.team = owner->relationsData.team;

    if (ai->state == AIState::hasTarget) {
        ai->passiveRotation = util::randomFloat() < 0.5 ? AI::PASSIVE_ROTATION : -AI::PASSIVE_ROTATION;
    }

    ai->aimSpeed = turret->bulletAccel;
    ai->movementSpeed = 0;

    reloadTime = owner->reloadTime;
    turret->calculateStatData();

    bool useAI = !(influencedByOwnerInputs && (owner->inputs->attemptingRepel() || owner->inputs->attemptingShot()));

    if (!useAI) {
        Vector wp = getWorldPosition();
        double flip = owner->inputs->attemptingRepel() ? -1.0 : 1.0;
        VectorAbstract delta = { (owner->inputs->mouse.x - wp.x) * flip, (owner->inputs->mouse.y - wp.y) * flip };
        VectorAbstract target = { wp.x + delta.x, wp.y + delta.y };

        if (ai->targetFilter(target) == false) {
            useAI = true;
        } else {
            inputs->flags |= InputFlags::leftclick;
            positionData.setAngle(std::atan2(delta.y, delta.x));
            ai->state = AIState::hasTarget;
        }
    }

    if (useAI) {
        if (ai->state == AIState::idle) {
            ObjectEntity* rot = rotator.get();
            if (rot) positionData.setAngle(slotAngle + rot->positionData.angle + ai->passiveRotation);
            else positionData.setAngle(positionData.angle + ai->passiveRotation);
            turret->attemptingShot = false;
        } else {
            Vector wp = getWorldPosition();
            positionData.setAngle(std::atan2(ai->inputs->mouse.y - wp.y, ai->inputs->mouse.x - wp.x));
        }
    }
}
