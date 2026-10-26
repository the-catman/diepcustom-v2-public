#include "Entity/Tank/Barrel.h"
#include "Entity/Tank/TankBody.h"
#include "Entity/Tank/BarrelAddons.h"
#include "Entity/Tank/Projectile/Bullet.h"
#include "Entity/Tank/Projectile/Trap.h"
#include "Entity/Tank/Projectile/BaseDrone.h"
#include "Entity/Tank/Projectile/Drone.h"
#include "Entity/Tank/Projectile/Rocket.h"
#include "Entity/Tank/Projectile/Skimmer.h"
#include "Entity/Tank/Projectile/Minion.h"
#include "Entity/Tank/Projectile/Flame.h"
#include "Entity/Tank/Projectile/CrocSkimmer.h"
#include "Entity/Tank/Projectile/Swarm.h"
#include "Entity/Tank/Projectile/NecromancerSquare.h"
#include "Entity/Misc/MazeWall.h"
#include "Native/Arena.h"
#include "Game.h"
#include "Util.h"
#include "Const/Enums.h"

#include <cmath>
#include <optional>

Barrel::~Barrel() { delete shootCycle; for (BarrelAddon* a : addons) delete a; }

// ---- ShootCycle ----

ShootCycle::ShootCycle(Barrel* barrel) : barrelEntity(barrel) {
    barrelEntity->barrelData.setReloadTime(barrelEntity->tank->reloadTime * barrelEntity->definition->reload);
    reloadTime = pos = barrelEntity->barrelData.reloadTime;
}

void ShootCycle::tick() {
    const double newReloadTime = barrelEntity->tank->reloadTime * barrelEntity->definition->reload;
    const std::string& btype = barrelEntity->definition->bullet.type;
    const bool alwaysShoot = barrelEntity->definition->forceFire.value_or(false)
        || btype == "drone" || btype == "basedrone" || btype == "minion";

    if (pos >= newReloadTime) {
        if (!barrelEntity->attemptingShot && !alwaysShoot) { pos = newReloadTime; return; }
        if (barrelEntity->definition->droneCount.has_value() && barrelEntity->droneCount >= *barrelEntity->definition->droneCount) {
            pos = newReloadTime;
            return;
        }
    }

    if (pos >= newReloadTime * (1 + barrelEntity->definition->delay)) {
        barrelEntity->barrelData.setReloadTime(newReloadTime);
        barrelEntity->shoot();
        pos = newReloadTime * barrelEntity->definition->delay;
    }

    pos += 1;
}

// ---- Barrel ----

Barrel::Barrel(BarrelBase* owner, BarrelDefinition* barrelDefinition)
    : ObjectEntity(owner->game), definition(barrelDefinition), tank(owner), barrelData(this) {
    styleData.setColor(definition->color.value_or(Color::Barrel));
    physicsData.setSides(2);
    if (barrelDefinition->isTrapezoid) physicsData.setFlags(physicsData.flags | PhysicsFlags::isTrapezoid);

    setParent(owner);
    relationsData.setOwner(owner);
    relationsData.setTeam(owner->relationsData.team);

    const double sf = owner->scaleFactor;
    const double size = definition->size * sf;
    physicsData.setSize(size);
    physicsData.setWidth(definition->width * sf);

    const double dist = definition->distance.value_or(0.0) * sf;
    positionData.setAngle(definition->angle + definition->trapezoidDirection);
    positionData.setX(std::cos(definition->angle) * (size / 2 + dist) - std::sin(definition->angle) * definition->offset * sf);
    positionData.setY(std::sin(definition->angle) * (size / 2 + dist) + std::cos(definition->angle) * definition->offset * sf);

    if (!barrelDefinition->addon.empty()) {
        auto it = BarrelAddonById.find(barrelDefinition->addon);
        if (it != BarrelAddonById.end()) addons.push_back(it->second(this));
    }

    barrelData.setTrapezoidDirection(barrelDefinition->trapezoidDirection);
    shootCycle = new ShootCycle(this);

    calculateStatData();
}

void Barrel::shoot() {
    barrelData.setFlags(barrelData.flags ^ BarrelFlags::hasShot);

    TankDefinition* tankDef = nullptr;
    if (TankBody::isTank(rootParent)) tankDef = static_cast<TankBody*>(rootParent)->definition;

    const std::string& btype = definition->bullet.type;

    const double scatterAngle = (M_PI / 180.0) * definition->bullet.scatterRate * (util::randomFloat() - 0.5) * 10;
    const double angle = definition->angle + scatterAngle + tank->positionData.angle;

    rootParent->addVelocity(angle + M_PI, definition->recoil * 2);

    ObjectEntity* projectile = nullptr;

    if (btype == "skimmer") {
        double rot = tank->inputs->attemptingRepel() ? -Skimmer::BASE_ROTATION : Skimmer::BASE_ROTATION;
        projectile = new Skimmer(this, tank, tankDef, angle, rot);
    } else if (btype == "rocket") {
        new Rocket(this, tank, tankDef, angle);
    } else if (btype == "bullet") {
        projectile = new Bullet(this, tank, tankDef, angle);
        if (tankDef && tankDef->id == Tank::ArenaCloser) {
            projectile->positionData.setFlags(projectile->positionData.flags | PositionFlags::canMoveThroughWalls);
        }
    } else if (btype == "trap") {
        projectile = new Trap(this, tank, tankDef, angle);
    } else if (btype == "drone") {
        projectile = new Drone(this, tank, tankDef, angle);
    } else if (btype == "basedrone") {
        projectile = new BaseDrone(this, tank, tankDef, angle);
    } else if (btype == "necrodrone") {
        projectile = new NecromancerSquare(this, tank, tankDef, angle);
    } else if (btype == "swarm") {
        projectile = new Swarm(this, tank, tankDef, angle);
    } else if (btype == "minion") {
        projectile = new Minion(this, tank, tankDef, angle);
    } else if (btype == "flame") {
        projectile = new Flame(this, tank, tankDef, angle);
    } else if (btype == "wall") {
        double mx = std::floor(tank->inputs->mouse.x / 50 + 0.5) * 50;
        double my = std::floor(tank->inputs->mouse.y / 50 + 0.5) * 50;
        MazeWall* w = new MazeWall(game->arena, mx, my, 250, 250);
        projectile = w;
        game->scheduleTimeout(60.0 * 1000, [w]() { if (Entity::exists(w)) w->delete_(); });
    } else if (btype == "croc") {
        projectile = new CrocSkimmer(this, tank, tankDef, angle);
    } else {
        util::log("Ignoring attempt to spawn projectile of type ", btype);
    }

    if (projectile) {
        if (definition->bullet.sides.has_value()) projectile->physicsData.setSides(*definition->bullet.sides);
        if (definition->bullet.color.has_value()) projectile->styleData.setColor(*definition->bullet.color);
    }
}

void Barrel::calculateStatData() {
    const double newReloadTime = tank->reloadTime * definition->reload;

    if (newReloadTime != shootCycle->reloadTime) {
        shootCycle->pos *= newReloadTime / shootCycle->reloadTime;
        shootCycle->reloadTime = newReloadTime;
        barrelData.setReloadTime(newReloadTime);
    }

    int statLevel = 0;
    // cameraEntity may be a non-camera self-cast (e.g. BaseDrones); dispatch via the Entity base.
    Entity* camEnt = (Entity*)(CameraEntity*)tank->cameraEntity;
    if (CameraData* cd = camEnt ? camEnt->getCameraData() : nullptr) {
        statLevel = (int)cd->statLevels.values[Stat::BulletSpeed];
    }
    bulletAccel = (20 + statLevel * 3) * definition->bullet.speed;
}

void Barrel::tick(int tick) {
    relationsData.team = tank->relationsData.team;

    if (!tank->rootParent->deletionAnimation) {
        attemptingShot = tank->inputs->attemptingShot();
        shootCycle->tick();
    }

    ObjectEntity::tick(tick);
}
