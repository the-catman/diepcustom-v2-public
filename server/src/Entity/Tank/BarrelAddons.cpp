#include "Entity/Tank/BarrelAddons.h"
#include "Entity/Tank/Barrel.h"
#include "Const/Enums.h"

// ---- BarrelAddon ----

BarrelAddon::BarrelAddon(Barrel* barrel) : game(barrel->game), owner(barrel) {}

// ---- TrapLauncher ----

TrapLauncher::TrapLauncher(Barrel* barrel) : ObjectEntity(barrel->game), barrelEntity(barrel) {
    setParent(barrel);
    relationsData.setTeam(barrel);
    physicsData.setFlags(PhysicsFlags::isTrapezoid | PhysicsFlags::doChildrenCollision);
    styleData.setColor(Color::Barrel);
    physicsData.setSides(2);
    physicsData.setWidth(barrel->physicsData.width);
    const double sz = barrel->physicsData.width * (20.0 / 42);
    physicsData.setSize(sz);
    positionData.setX((barrel->physicsData.size + sz) / 2);
}

void TrapLauncher::resize() {
    physicsData.setSides(2);
    physicsData.setWidth(barrelEntity->physicsData.width);
    const double sz = barrelEntity->physicsData.width * (20.0 / 42);
    physicsData.setSize(sz);
    positionData.setX((barrelEntity->physicsData.size + physicsData.size) / 2);
}

void TrapLauncher::tick(int tick) {
    ObjectEntity::tick(tick);
    resize();
}

// ---- TrapLauncherAddon ----

TrapLauncherAddon::TrapLauncherAddon(Barrel* owner) : BarrelAddon(owner) {
    launcherEntity = new TrapLauncher(owner);
}

// ---- PurpleBarrelAddon ----

PurpleBarrelAddon::PurpleBarrelAddon(Barrel* owner) : BarrelAddon(owner) {
    owner->styleData.setColor(Color::TeamPurple);
}

// ---- registry ----

const std::unordered_map<std::string, BarrelAddonFactory> BarrelAddonById = {
    { "trapLauncher",  [](Barrel* o) -> BarrelAddon* { return new TrapLauncherAddon(o); } },
    { "purplebarrel",  [](Barrel* o) -> BarrelAddon* { return new PurpleBarrelAddon(o); } },
};
