#include "Entity/Misc/BaseDrones.h"
#include "Entity/AI.h"
#include "Entity/Misc/TeamBase.h"
#include "Entity/Tank/Barrel.h"
#include "Native/Camera.h"
#include "Const/Enums.h"

BaseDrones::BaseDrones(TeamBase* base, int droneCount)
    : ObjectEntity(base->game) {
    droneSpawnerDef.angle = 0;
    droneSpawnerDef.offset = 0;
    droneSpawnerDef.size = 95;
    droneSpawnerDef.width = 42;
    droneSpawnerDef.delay = 0;
    droneSpawnerDef.reload = 0;
    droneSpawnerDef.recoil = 0;
    droneSpawnerDef.isTrapezoid = true;
    droneSpawnerDef.trapezoidDirection = 0;
    droneSpawnerDef.addon = "";
    droneSpawnerDef.droneCount = droneCount;
    droneSpawnerDef.canControlDrones = true;
    droneSpawnerDef.bullet.type = "basedrone";
    droneSpawnerDef.bullet.sizeRatio = 1;
    droneSpawnerDef.bullet.health = 1000;
    droneSpawnerDef.bullet.damage = 1;
    droneSpawnerDef.bullet.speed = 2.25;
    droneSpawnerDef.bullet.scatterRate = 1;
    droneSpawnerDef.bullet.lifeLength = -1;
    droneSpawnerDef.bullet.absorbtionFactor = 1;

    // Self cast as CameraEntity; Barrel only reads it via the BarrelBase fields.
    cameraEntity = reinterpret_cast<CameraEntity*>(this);
    reloadTime = 15;
    inputs = std::make_shared<Inputs>();

    isPhysical = false;
    physicsData.sides = 0;
    physicsData.size = 100; // drone resting return threshold = size * 1.2 = 120
    physicsData.absorbtionFactor = 0;
    physicsData.pushFactor = 0;
    physicsData.setFlags(physicsData.flags | PhysicsFlags::isBase);

    positionData.x = base->positionData.x;
    positionData.y = base->positionData.y;

    relationsData.setOwner(base);
    relationsData.setTeam(base->relationsData.team);

    styleData.setColor(base->styleData.color);

    droneSpawner = new Barrel(this, &droneSpawnerDef);
    uint32_t vis = styleData.flags ^ StyleFlags::isVisible;
    droneSpawner->styleData.setFlags(vis);
    styleData.setFlags(vis);
}
