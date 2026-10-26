#include "Entity/Tank/Projectile/Skimmer.h"
#include "Entity/AI.h"
#include "Entity/Tank/Barrel.h"
#include "Const/Enums.h"

static BarrelDefinition SkimmerBarrelDefinition = {
    /* angle */ 0,
    /* offset */ 0,
    /* distance */ std::nullopt,
    /* size */ 70,
    /* width */ 50.4,
    /* delay */ 0,
    /* reload */ 0.35,
    /* recoil */ 0,
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
        /* health */ 0.3,
        /* damage */ 3.0 / 5,
        /* speed */ 1.1,
        /* scatterRate */ 1,
        /* lifeLength */ 0.25,
        /* absorbtionFactor */ 1,
        /* color */ std::nullopt,
        /* sides */ std::nullopt,
    }
};

Skimmer::Skimmer(Barrel* barrel, BarrelBase* tank, TankDefinition* tankDefinition, double shootAngle, double direction)
    : Bullet(barrel, tank, tankDefinition, shootAngle)
{
    scaleFactor = physicsData.size / 50;
    rotationPerTick = direction;

    cameraEntity = tank->cameraEntity;
    reloadTime = 15;
    inputs = std::make_shared<Inputs>();
    inputs->flags |= InputFlags::leftclick;

    s1Def = SkimmerBarrelDefinition;
    s1 = new Barrel(this, &s1Def);
    s1->styleData.setColor(styleData.color);

    s2Def = SkimmerBarrelDefinition;
    s2Def.angle += M_PI;
    s2 = new Barrel(this, &s2Def);
    s2->styleData.setColor(styleData.color);

    scale(1);
    s1->physicsData.width = s1->definition->width;
    s2->physicsData.width = s2->definition->width;
}

void Skimmer::tick(int tick) {
    if (Entity::exists(tank)) reloadTime = tank->reloadTime;
    positionData.setAngle(positionData.angle + rotationPerTick);

    Bullet::tick(tick);

    if (deletionAnimation || !Entity::exists(this)) return;
    s1->physicsData.setWidth(s1->definition->width);
    s2->physicsData.setWidth(s2->definition->width);
}
