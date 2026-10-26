#pragma once

#include "Entity/Live.h"
#include "Const/TankDefinitions.h"

class Barrel;

class Bullet : public LivingEntity {
public:
    Bullet(Barrel* barrel, BarrelBase* tank, TankDefinition* tankDefinition, double shootAngle);

    void onKill(LivingEntity* killedEntity, LivingEntity* weapon) override;
    void tick(int tick) override;

protected:
    Ref<Barrel> barrelEntity;
    int spawnTick = 0;
    double baseAccel = 0;
    double baseSpeed = 0;
    double deathAccelFactor = 0.5;
    double lifeLength = 0;
    double movementAngle = 0;
    TankDefinition* tankDefinition = nullptr;
    bool usePosAngle = false;
    Ref<BarrelBase> tank;
    // let the kill credit the camera after the tank is deleted
    Ref<CameraEntity> ownerCamera;
    bool ownerWasTank = false;
};
