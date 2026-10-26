#include "Entity/Tank/Projectile/Bullet.h"
#include "Native/Manager.h"
#include "Entity/Tank/Barrel.h"
#include "Entity/Tank/TankBody.h"
#include "Native/Camera.h"
#include "Physics/CollisionManager.h"
#include "Native/Entity.h"
#include "Const/Enums.h"
#include "Util.h"
#include "Game.h"

#include <cmath>

Bullet::Bullet(Barrel* barrel, BarrelBase* tank, TankDefinition* tankDefinition, double shootAngle)
    : LivingEntity(barrel->game), tankDefinition(tankDefinition), tank(tank)
{
    entityTags |= EntityTags::isBullet;
    arenaMobID = "Bullet";
    movementAngle = shootAngle;
    barrelEntity = barrel;
    spawnTick = barrel->game->tick;

    ownerCamera = tank->cameraEntity;
    ownerWasTank = TankBody::isTank(tank);
    if (!ownerWasTank && (tank->entityTags & EntityTags::isBullet)) {
        // sub-bullet of a Rocket/Skimmer/Minion: inherit its credit target
        Bullet* parent = (Bullet*)tank;
        ownerWasTank = parent->ownerWasTank;
        ownerCamera = parent->ownerCamera;
    }

    const BulletDefinition& bulletDef = barrel->definition->bullet;
    double scaleFactor = tank->scaleFactor;

    Entity* camEnt = (Entity*)(CameraEntity*)tank->cameraEntity; // may be a non-camera self-cast; dispatch via Entity base
    CameraData* camData = camEnt ? camEnt->getCameraData() : nullptr;
    double bulletDamage = camData ? camData->statLevels.values[Stat::BulletDamage] : 0;
    double bulletPenetration = camData ? camData->statLevels.values[Stat::BulletPenetration] : 0;

    relationsData.setTeam(barrel->relationsData.team);
    relationsData.setOwner(tank);

    tank->rootParent->styleData.setZIndex(barrel->game->entities->zIndex++);

    physicsData.setSides(1);
    physicsData.setFlags(physicsData.flags | PhysicsFlags::noOwnTeamCollision | PhysicsFlags::canEscapeArena);
    if (tank->positionData.flags & PositionFlags::canMoveThroughWalls)
        positionData.setFlags(positionData.flags | PositionFlags::canMoveThroughWalls);

    physicsData.setSize((barrel->physicsData.width / 2) * bulletDef.sizeRatio);
    styleData.setColor(tank->rootParent->styleData.color);
    styleData.setFlags(styleData.flags | StyleFlags::hasNoDmgIndicator);
    healthData.setFlags(HealthFlags::hiddenHealthbar);

    physicsData.setAbsorbtionFactor(bulletDef.absorbtionFactor);
    physicsData.setPushFactor(((7.0 / 3) + bulletDamage) * bulletDef.damage * bulletDef.absorbtionFactor);

    baseAccel = barrel->bulletAccel;
    baseSpeed = barrel->bulletAccel + 30 - util::randomFloat() * bulletDef.scatterRate;

    double hp = (1.5 * bulletPenetration + 2) * bulletDef.health;
    healthData.setHealth(hp);
    healthData.setMaxHealth(hp);

    damagePerTick = (7 + bulletDamage * 3) * bulletDef.damage;
    minDamageMultiplier = 0.25;
    maxDamageMultiplier = 1;

    lifeLength = bulletDef.lifeLength * 75;

    Vector worldPos = tank->getWorldPosition();
    double distance = barrel->definition->distance.value_or(0);
    double offset = barrel->definition->offset;

    positionData.setX(worldPos.x
        + std::cos(shootAngle) * barrel->physicsData.size
        - std::sin(shootAngle) * offset * scaleFactor
        + std::cos(shootAngle) * distance);
    positionData.setY(worldPos.y
        + std::sin(shootAngle) * barrel->physicsData.size
        + std::cos(shootAngle) * offset * scaleFactor
        + std::sin(shootAngle) * distance);
    positionData.setAngle(shootAngle);

    // grid only rebuilds in preTick; insert now so cameras see the shot this tick
    game->entities->collisionManager->insert(this);
}

void Bullet::onKill(LivingEntity* killedEntity, LivingEntity* weapon) {
    BarrelBase* owner = tank.get();
    if (owner) { owner->onKill(killedEntity, weapon); return; }
    // owner already deleted; credit the kill through the saved camera ref
    if (ownerWasTank) TankBody::grantKillRewards(ownerCamera, killedEntity, nullptr, weapon);
}

void Bullet::tick(int tick) {
    LivingEntity::tick(tick);

    if (tick == spawnTick + 1) addVelocity(movementAngle, baseSpeed);
    else maintainVelocity(usePosAngle ? positionData.angle : movementAngle, baseAccel);

    if (tick - spawnTick >= lifeLength) destroy(true);

    // raw write to avoid triggering a wire update
    if ((relationsData.team ? relationsData.team->entityState : 0) & EntityStateFlags::needsDelete)
        relationsData.team = nullptr;
}
