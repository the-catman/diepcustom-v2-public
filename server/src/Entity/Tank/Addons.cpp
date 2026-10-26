#include "Entity/Tank/Addons.h"
#include "Entity/Tank/TankBody.h"
#include "Entity/Tank/Barrel.h"
#include "Entity/Tank/AutoTurret.h"
#include "Entity/Live.h"
#include "Util.h"
#include "Const/Enums.h"

#include <cmath>
#include <optional>

// ---- Addon ----

Addon::Addon(BarrelBase* ownerIn) : game(ownerIn->game), owner(ownerIn) {}

GuardObject* Addon::createGuard(double sides, double sizeRatio, double offsetAngle, double radiansPerTick) {
    return new GuardObject(game, owner, sides, sizeRatio, offsetAngle, radiansPerTick);
}

static const BarrelDefinition AutoTurretMiniDefinition = {
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
        .damage = 0.4,
        .speed = 1.2,
        .scatterRate = 1,
        .lifeLength = 1,
        .absorbtionFactor = 1,
        .color = std::nullopt,
        .sides = std::nullopt
    }
};

void Addon::createAutoTurrets(int count) {
    const double rotPerTick = AI::PASSIVE_ROTATION;
    const double MAX_ANGLE_RANGE = util::PI2 / 4;
    const double ROT_OFFSET = 0.8;

    GuardObject* rotator = createGuard(1, 0.1, 0, rotPerTick);
    if (rotator->styleData.flags & StyleFlags::isVisible) rotator->styleData.setFlags(rotator->styleData.flags ^ StyleFlags::isVisible);

    for (int i = 0; i < count; ++i) {
        AutoTurret* base = new AutoTurret(rotator, &AutoTurretMiniDefinition);
        base->influencedByOwnerInputs = true;

        const double angle = util::PI2 * ((double)i / count);
        base->slotAngle = angle;
        base->rotator = rotator;
        base->ai->inputs->mouse.setAngle(angle);
        base->ai->passiveRotation = rotPerTick;

        base->ai->targetFilter = [base, angle, rotator, MAX_ANGLE_RANGE](const VectorAbstract& targetPos) -> bool {
            Vector pos = base->getWorldPosition();
            double angleToTarget = std::atan2(targetPos.y - pos.y, targetPos.x - pos.x);
            double delta = util::normalizeAngle(angleToTarget - (angle + rotator->positionData.angle));
            return delta < MAX_ANGLE_RANGE || delta > (util::PI2 - MAX_ANGLE_RANGE);
        };

        base->positionData.setY(owner->physicsData.size * std::sin(angle) * ROT_OFFSET);
        base->positionData.setX(owner->physicsData.size * std::cos(angle) * ROT_OFFSET);

        if (base->styleData.flags & StyleFlags::showsAboveParent) base->styleData.setFlags(base->styleData.flags ^ StyleFlags::showsAboveParent);
        base->physicsData.setFlags(base->physicsData.flags | PositionFlags::absoluteRotation);
    }
}

// ---- GuardObject ----

GuardObject::GuardObject(GameServer* game, BarrelBase* ownerIn, double sides, double sf, double offsetAngle, double rpt)
    : ObjectEntity(game), radiansPerTick(rpt), guardOwner(ownerIn) {
    cameraEntity = ownerIn->cameraEntity;
    inputs = ownerIn->inputs;
    reloadTime = ownerIn->reloadTime;

    sf *= M_SQRT1_2;
    scaleFactor = sf;

    setParent(ownerIn);
    relationsData.setOwner(ownerIn);
    relationsData.setTeam(ownerIn->relationsData.team);

    styleData.setColor(Color::Border);
    positionData.setFlags(positionData.flags | PositionFlags::absoluteRotation);
    positionData.setAngle(offsetAngle);
    physicsData.setSides(sides);
    physicsData.setSize(ownerIn->physicsData.size * sf);
}

void GuardObject::onKill(LivingEntity* killedEntity, LivingEntity* weapon) {
    if (!LivingEntity::isLive(guardOwner)) return;
    guardOwner->onKill(killedEntity, weapon);
}

void GuardObject::tick(int tick) {
    reloadTime = guardOwner->reloadTime;
    positionData.setAngle(positionData.angle + radiansPerTick);
}

// ---- Concrete Addons ----

class SpikeAddon : public Addon {
public:
    explicit SpikeAddon(BarrelBase* owner) : Addon(owner) {
        createGuard(3, 1.3, 0, 0.17);
        createGuard(3, 1.3, M_PI / 3, 0.17);
        createGuard(3, 1.3, M_PI / 6, 0.17);
        createGuard(3, 1.3, M_PI / 2, 0.17);
    }
};

class DomBaseAddon : public Addon {
public:
    explicit DomBaseAddon(BarrelBase* owner) : Addon(owner) {
        createGuard(6, 1.24, 0, 0);
    }
};

class SmasherAddon : public Addon {
public:
    explicit SmasherAddon(BarrelBase* owner) : Addon(owner) {
        createGuard(6, 1.15, 0, 0.1);
    }
};

class LandmineAddon : public Addon {
public:
    explicit LandmineAddon(BarrelBase* owner) : Addon(owner) {
        createGuard(6, 1.15, 0, 0.1);
        createGuard(6, 1.15, 0, 0.05);
    }
};

class LauncherAddon : public Addon {
public:
    explicit LauncherAddon(BarrelBase* owner) : Addon(owner) {
        ObjectEntity* launcher = new ObjectEntity(game);
        const double sizeRatio = 65.5 * M_SQRT2 / 50;
        const double widthRatio = 33.6 / 50;
        const double size = owner->physicsData.size;

        launcher->setParent(owner);
        launcher->relationsData.setOwner(owner);
        launcher->relationsData.setTeam(owner->relationsData.team);

        launcher->physicsData.setSize(sizeRatio * size);
        launcher->physicsData.setWidth(widthRatio * size);
        launcher->positionData.setX(launcher->physicsData.size / 2);

        launcher->styleData.setColor(Color::Barrel);
        launcher->physicsData.setFlags(launcher->physicsData.flags | PhysicsFlags::isTrapezoid);
        launcher->physicsData.setSides(2);
    }
};

class AutoTurretAddon : public Addon {
public:
    explicit AutoTurretAddon(BarrelBase* owner) : Addon(owner) {
        new AutoTurret(owner);
    }
};

class AutoSmasherAddon : public Addon {
public:
    explicit AutoSmasherAddon(BarrelBase* owner) : Addon(owner) {
        createGuard(6, 1.15, 0, 0.1);
        AutoTurret* turret = new AutoTurret(owner);
        turret->influencedByOwnerInputs = true;
    }
};

class Auto5Addon : public Addon {
public:
    explicit Auto5Addon(BarrelBase* owner) : Addon(owner) {
        createAutoTurrets(5);
    }
};

class Auto3Addon : public Addon {
public:
    explicit Auto3Addon(BarrelBase* owner) : Addon(owner) {
        createAutoTurrets(3);
    }
};

class PronouncedAddon : public Addon {
public:
    explicit PronouncedAddon(BarrelBase* owner) : Addon(owner) {
        ObjectEntity* pronounce = new ObjectEntity(game);
        const double size = owner->physicsData.size;

        pronounce->setParent(owner);
        pronounce->relationsData.setOwner(owner);
        pronounce->relationsData.setTeam(owner->relationsData.team);

        pronounce->physicsData.setSize((50.0 / 50) * size);
        pronounce->physicsData.setWidth((42.0 / 50) * size);
        pronounce->positionData.setX((40.0 / 50) * size);
        pronounce->positionData.setAngle(M_PI);

        pronounce->styleData.setColor(Color::Barrel);
        pronounce->physicsData.setFlags(pronounce->physicsData.flags | PhysicsFlags::isTrapezoid);
        pronounce->physicsData.setSides(2);
    }
};


class PronouncedDomAddon : public Addon {
public:
    explicit PronouncedDomAddon(BarrelBase* owner) : Addon(owner) {
        ObjectEntity* pronounce = new ObjectEntity(game);
        const double size = owner->physicsData.size;

        pronounce->setParent(owner);
        pronounce->relationsData.setOwner(owner);
        pronounce->relationsData.setTeam(owner->relationsData.team);

        pronounce->physicsData.setSize((22.0 / 50) * size);
        pronounce->physicsData.setWidth((35.0 / 50) * size);
        pronounce->positionData.setX((50.0 / 50) * size);
        pronounce->positionData.setAngle(M_PI);

        pronounce->styleData.setColor(Color::Barrel);
        pronounce->physicsData.setFlags(pronounce->physicsData.flags | PhysicsFlags::isTrapezoid);
        pronounce->physicsData.setSides(2);
    }
};

class WeirdSpikeAddon : public Addon {
public:
    explicit WeirdSpikeAddon(BarrelBase* owner) : Addon(owner) {
        createGuard(3, 1.5, 0, 0.17);
        createGuard(3, 1.5, 0, -0.16);
    }
};

class Auto2Addon : public Addon {
public:
    explicit Auto2Addon(BarrelBase* owner) : Addon(owner) {
        createAutoTurrets(2);
    }
};

class Auto7Addon : public Addon {
public:
    explicit Auto7Addon(BarrelBase* owner) : Addon(owner) {
        createAutoTurrets(7);
    }
};

class AutoRocketAddon : public Addon {
public:
    explicit AutoRocketAddon(BarrelBase* owner) : Addon(owner) {
        static const BarrelDefinition rocketDef = {
            .angle = 0,
            .offset = 0,
            .distance = std::nullopt,
            .size = 40,
            .width = 26.25,
            .delay = 0,
            .reload = 2,
            .recoil = 0.75,
            .isTrapezoid = true,
            .trapezoidDirection = 3.141592653589793,
            .addon = "",
            .droneCount = std::nullopt,
            .canControlDrones = std::nullopt,
            .forceFire = std::nullopt,
            .color = std::nullopt,
            .bullet = {
                .type = "rocket",
                .sizeRatio = 1,
                .health = 2.5,
                .damage = 0.5,
                .speed = 0.3,
                .scatterRate = 1,
                .lifeLength = 0.75,
                .absorbtionFactor = 0.1,
                .color = std::nullopt,
                .sides = std::nullopt
            }
        };

        AutoTurret* base = new AutoTurret(owner, &rocketDef);
        LauncherAddon launcher(base); // transient: attaches a launcher entity to base in its ctor
        (void)launcher;
        base->turret->styleData.setZIndex(base->turret->styleData.zIndex + 2);
    }
};

class SpieskAddon : public Addon {
public:
    explicit SpieskAddon(BarrelBase* owner) : Addon(owner) {
        createGuard(4, 1.3, 0, 0.17);
        createGuard(4, 1.3, M_PI / 6, 0.17);
        createGuard(4, 1.3, 2 * M_PI / 6, 0.17);
    }
};

const std::unordered_map<std::string, AddonFactory> AddonById = {
    { "spike",          [](BarrelBase* o) -> Addon* { return new SpikeAddon(o); } },
    { "dombase",        [](BarrelBase* o) -> Addon* { return new DomBaseAddon(o); } },
    { "launcher",       [](BarrelBase* o) -> Addon* { return new LauncherAddon(o); } },
    { "dompronounced",  [](BarrelBase* o) -> Addon* { return new PronouncedDomAddon(o); } },
    { "auto5",          [](BarrelBase* o) -> Addon* { return new Auto5Addon(o); } },
    { "auto3",          [](BarrelBase* o) -> Addon* { return new Auto3Addon(o); } },
    { "autosmasher",    [](BarrelBase* o) -> Addon* { return new AutoSmasherAddon(o); } },
    { "pronounced",     [](BarrelBase* o) -> Addon* { return new PronouncedAddon(o); } },
    { "smasher",        [](BarrelBase* o) -> Addon* { return new SmasherAddon(o); } },
    { "landmine",       [](BarrelBase* o) -> Addon* { return new LandmineAddon(o); } },
    { "autoturret",     [](BarrelBase* o) -> Addon* { return new AutoTurretAddon(o); } },
    { "weirdspike",     [](BarrelBase* o) -> Addon* { return new WeirdSpikeAddon(o); } },
    { "auto7",          [](BarrelBase* o) -> Addon* { return new Auto7Addon(o); } },
    { "auto2",          [](BarrelBase* o) -> Addon* { return new Auto2Addon(o); } },
    { "autorocket",     [](BarrelBase* o) -> Addon* { return new AutoRocketAddon(o); } },
    { "spiesk",         [](BarrelBase* o) -> Addon* { return new SpieskAddon(o); } },
};
