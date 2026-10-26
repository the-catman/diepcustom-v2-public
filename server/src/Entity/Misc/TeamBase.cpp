#include "Entity/Misc/TeamBase.h"
#include "Entity/Misc/TeamEntity.h"
#include "Entity/Misc/BaseDrones.h"
#include "Game.h"
#include "Const/Enums.h"

TeamBase::TeamBase(GameServer* game, Entity* team, double x, double y, double width, double height, bool shielded, int droneSpawnerCount, int droneCount)
    : LivingEntity(game) {
    setGlobalEntity();

    relationsData.setTeam(team);

    if (TeamEntity::isTeamEntity(team)) static_cast<TeamEntity*>(team)->base = this;

    positionData.x = x;
    positionData.y = y;

    physicsData.width = width;
    physicsData.size = height;
    physicsData.sides = 2;
    physicsData.setFlags(physicsData.flags | PhysicsFlags::noOwnTeamCollision | PhysicsFlags::isBase);
    physicsData.pushFactor = 2;
    physicsData.absorbtionFactor = 0;

    styleData.setOpacity(0.1);
    styleData.setBorderWidth(0);
    TeamData* td = team ? team->getTeamData() : nullptr;
    styleData.setColor(td ? td->teamColor : Color::Border);
    styleData.setFlags(styleData.flags | StyleFlags::renderFirst | StyleFlags::hasNoDmgIndicator);

    damagePerTick = 5;
    minDamageMultiplier = 1;
    maxDamageMultiplier = 1;
    damageReduction = 0;

    if (!shielded) {
        physicsData.pushFactor = 0;
        damagePerTick = 0;
    } else {
        createDrones(droneSpawnerCount, droneCount);
    }

    healthData.setFlags(healthData.flags | HealthFlags::hiddenHealthbar);
    healthData.setHealth(0xABCFF);
    healthData.maxHealth = 0xABCFF;
}

void TeamBase::createDrones(int droneSpawnerCount, int droneCount) {
    for (int i = 0; i < droneSpawnerCount; ++i) {
        BaseDrones* spawner = new BaseDrones(this, droneCount);
        spawner->positionData.x = positionData.x;
        // Don't spread drones if the base is a square
        if (physicsData.width == physicsData.size) {
            spawner->positionData.y = positionData.y;
        } else {
            spawner->positionData.y = physicsData.width * (i + 1.0) / (droneSpawnerCount + 1) - physicsData.width / 2;
        }
    }
}

void TeamBase::tick(int tick) {
    healthData.setHealth(healthData.maxHealth);
    lastDamageTick = tick;
}
