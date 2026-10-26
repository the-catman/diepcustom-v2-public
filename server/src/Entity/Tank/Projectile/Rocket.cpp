#include "Entity/Tank/Projectile/Rocket.h"
#include "Entity/AI.h"
#include "Entity/Tank/Barrel.h"
#include "Const/Enums.h"

static BarrelDefinition RocketBarrelDefinition = {
    /* angle */ M_PI,
    /* offset */ 0,
    /* distance */ std::nullopt,
    /* size */ 70,
    /* width */ 37.8,
    /* delay */ 0,
    /* reload */ 0.15,
    /* recoil */ 3.3,
    /* isTrapezoid */ true,
    /* trapezoidDirection */ 0,
    /* addon */ "",
    /* droneCount */ std::nullopt,
    /* canControlDrones */ std::nullopt,
    /* forceFire */ std::nullopt,
    /* color */ std::nullopt,
    /* bullet */ {
        /* type */ "bullet",
        /* sizeRatio */ 1,
        /* health */ 0.3,
        /* damage */ 3.0 / 5,
        /* speed */ 1.5,
        /* scatterRate */ 5,
        /* lifeLength */ 0.1,
        /* absorbtionFactor */ 1,
        /* color */ std::nullopt,
        /* sides */ std::nullopt,
    }
};

Rocket::Rocket(Barrel* barrel, BarrelBase* tank, TankDefinition* tankDefinition, double shootAngle)
    : Bullet(barrel, tank, tankDefinition, shootAngle)
{
    scaleFactor = physicsData.size / 50;

    cameraEntity = tank->cameraEntity;
    reloadTime = 1;
    inputs = std::make_shared<Inputs>();

    rocketBarrel = new Barrel(this, &RocketBarrelDefinition);
    rocketBarrel->styleData.setColor(styleData.color);

    scale(1);
    rocketBarrel->physicsData.width = rocketBarrel->definition->width;
}

void Rocket::tick(int tick) {
    if (Entity::exists(tank)) reloadTime = tank->reloadTime;

    Bullet::tick(tick);

    if (deletionAnimation || !Entity::exists(this)) return;
    rocketBarrel->physicsData.setWidth(rocketBarrel->definition->width);
    if (tick - spawnTick >= reloadTime) inputs->flags |= InputFlags::leftclick;
}
