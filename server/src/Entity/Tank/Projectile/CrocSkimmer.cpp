#include "Entity/Tank/Projectile/CrocSkimmer.h"
#include "Entity/AI.h"
#include "Entity/Tank/Barrel.h"
#include "Const/Enums.h"

static BarrelDefinition CrocSkimmerBarrelDefinition = {
    /* angle */ M_PI / 2,
    /* offset */ 0,
    /* distance */ std::nullopt,
    /* size */ 70,
    /* width */ 42,
    /* delay */ 0,
    /* reload */ 0.5,
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
        /* speed */ 0.2,
        /* scatterRate */ 1,
        /* lifeLength */ 0.25,
        /* absorbtionFactor */ 1,
        /* color */ std::nullopt,
        /* sides */ std::nullopt,
    }
};

CrocSkimmer::CrocSkimmer(Barrel* barrel, BarrelBase* tank, TankDefinition* tankDefinition, double shootAngle)
    : Bullet(barrel, tank, tankDefinition, shootAngle)
{
    cameraEntity = tank->cameraEntity;
    reloadTime = 15;
    inputs = std::make_shared<Inputs>();
    inputs->flags |= InputFlags::leftclick;

    s1Def = CrocSkimmerBarrelDefinition;
    Barrel* s1 = new Barrel(this, &s1Def);
    s1->styleData.setColor(styleData.color);

    s2Def = CrocSkimmerBarrelDefinition;
    s2Def.angle += M_PI;
    Barrel* s2 = new Barrel(this, &s2Def);
    s2->styleData.setColor(styleData.color);
}

void CrocSkimmer::tick(int tick) {
    if (Entity::exists(tank)) reloadTime = tank->reloadTime;
    Bullet::tick(tick);
}
