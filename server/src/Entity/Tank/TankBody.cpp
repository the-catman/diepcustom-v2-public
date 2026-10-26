#include "Entity/Tank/TankBody.h"
#include "Entity/Tank/Barrel.h"
#include "Entity/Tank/Addons.h"
#include "Entity/Tank/Projectile/NecromancerSquare.h"
#include "Entity/Boss/AbstractBoss.h"
#include "Native/Arena.h"
#include "Native/Manager.h"
#include "Physics/CollisionManager.h"
#include "Game.h"
#include "Client.h"
#include "Util.h"
#include "Coder/Writer.h"
#include "Const/Enums.h"
#include "Const/Achievements.h"
#include "Const/TankDefinitions.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

TankBody::TankBody(GameServer* game, CameraEntity* cam, std::shared_ptr<Inputs> inp)
    : LivingEntity(game), nameData(this), scoreData(this), currentTank(Tank::Basic) {
    cameraEntity = cam;
    inputs = inp;

    physicsData.setSize(50);
    physicsData.setSides(1);
    styleData.setColor(Color::Tank);

    relationsData.setTeam(cam);
    relationsData.setOwner(cam);

    cam->getCameraData()->spawnTick = game->tick;
    cam->getCameraData()->flags |= CameraFlags::showingDeathStats;
    cam->getCameraData()->killedBy = "";

    styleData.setFlags(styleData.flags | StyleFlags::isFlashing);
    damageReduction = 0;
    if (game->playersOnMap) setGlobalEntity();

    damagePerTick = 5;
    maxDamageMultiplier = 6;
    setTank(Tank::Basic);

    entityTags |= EntityTags::isTank;
}

TankBody::~TankBody() { for (Addon* a : addons) delete a; }

void TankBody::setTank(int id) {
    for (ObjectEntity* child : children) {
        child->isChild = false;
        child->delete_();
    }
    children.clear();
    barrels.clear();
    for (Addon* a : addons) delete a;
    addons.clear();

    TankDefinition* tank = getTankById(id);
    if (!tank) throw std::runtime_error("Invalid tank ID");

    definition = tank;
    if (!Entity::exists(cameraEntity)) throw std::runtime_error("No camera");

    CameraData* cd = cameraEntity->getCameraData();

    physicsData.setSides(tank->sides);
    styleData.setOpacity(1.0);

    for (int i = 0; i < StatCount; ++i) {
        if (i >= (int)tank->stats.size()) { cd->statLimits.set(i, 0); cd->statNames.set(i, ""); continue; }
        cd->statLimits.set(i, tank->stats[i].max);
        cd->statNames.set(i, tank->stats[i].name);
        if (cd->statLevels.values[i] > tank->stats[i].max) {
            double excess = cd->statLevels.values[i] - tank->stats[i].max;
            cd->setStatsAvailable(cd->statsAvailable + excess);
            cd->statLevels.set(i, tank->stats[i].max);
        }
    }

    if (tank->baseSizeOverride.has_value()) {
        baseSize = *tank->baseSizeOverride;
    } else if (tank->sides == 4) {
        baseSize = M_SQRT2 * 32.5;
    } else if (tank->sides == 16) {
        baseSize = M_SQRT2 * 25;
    } else {
        baseSize = 50;
    }
    physicsData.setSize(baseSize * scaleFactor);
    physicsData.setAbsorbtionFactor(isInvulnerable ? 0.0 : tank->absorbtionFactor);

    if (tank->absorbtionFactor == 0) {
        positionData.setFlags(positionData.flags | PositionFlags::canMoveThroughWalls);
    } else if (positionData.flags & PositionFlags::canMoveThroughWalls) {
        positionData.setFlags(positionData.flags ^ PositionFlags::canMoveThroughWalls);
    }

    cd->setTank(id);
    currentTank = id;

    Client* client = cameraEntity->getClient();
    if (client && !tank->upgradeMessage.empty()) {
        client->notify(tank->upgradeMessage, 0x000000, 10000);
    }
    achievements::event(client, "classChange", {{"class", id}});

    if (!tank->preAddon.empty()) {
        auto it = AddonById.find(tank->preAddon);
        if (it != AddonById.end()) addons.push_back(it->second(this));
    }

    for (BarrelDefinition& bdef : tank->barrels) {
        barrels.push_back(new Barrel(this, &bdef));
    }

    if (!tank->postAddon.empty()) {
        auto it = AddonById.find(tank->postAddon);
        if (it != AddonById.end()) addons.push_back(it->second(this));
    }

    cd->setTankOverride(tank->name);
    cameraEntity->setFieldFactor(tank->fieldFactor);

    scale(1);
    calculateStatData();
}

void TankBody::grantKillRewards(CameraEntity* cam, LivingEntity* entity, LivingEntity* self, LivingEntity* weapon) {
    if (!cam) return;
    CameraData* cd = ((Entity*)cam)->getCameraData();
    if (!cd) return;

    if (Entity::exists(cd->player) && entity != self) {
        cam->addScore(entity->scoreReward);
    }

    NameData* nd = entity->getNameData();
    if (nd && !(nd->flags & NameFlags::hiddenName)) {
        Client* client = cam->getClient();
        if (client) {
            std::string msg = "You've killed " + (nd->name.empty() ? "an unnamed tank" : nd->name);
            client->notify(msg);
        }
    }

    if (entity == self) return;
    bool victimIsTank = TankBody::isTank(entity);
    achievements::event(cam->getClient(), "kill", {
        {"weapon.isTank", TankBody::isTank(weapon)},
        {"victim.arenaMobID", entity->arenaMobID},
        {"victim.isTank", victimIsTank},
        {"victim.isBoss", AbstractBoss::isBoss(entity)},
        {"victim.isShiny", (entity->entityTags & EntityTags::isShiny) != 0},
        {"class", (int)cd->tank},
        {"victim.class", victimIsTank ? ((TankBody*)entity)->currentTank : -1},
    });
}

void TankBody::onKill(LivingEntity* entity, LivingEntity* weapon) {
    grantKillRewards(cameraEntity, entity, this, weapon);

    if (entity->arenaMobID == "square" && definition->flags.canClaimSquares && !barrels.empty()) {
        int reloadLevel = 0;
        if (CameraEntity* cam = cameraEntity.get()) {
            if (CameraData* cdata = ((Entity*)cam)->getCameraData()) reloadLevel = (int)cdata->statLevels.values[Stat::Reload];
        }
        int maxDrones = 11 + reloadLevel;

        std::vector<Barrel*> shootable;
        for (Barrel* b : barrels) {
            if (b->definition->bullet.type == "necrodrone" && b->droneCount < maxDrones) {
                shootable.push_back(b);
            }
        }
        if (!shootable.empty()) {
            Barrel* chosen = util::randomFrom(shootable);

            NecromancerSquare* sunchip = NecromancerSquare::fromShape(chosen, this, definition, entity);
            game->entities->replaceEntity(entity, sunchip);
            sunchip->ai->owner = sunchip;
        }
    }
}

void TankBody::setInvulnerability(bool v) {
    if (styleData.flags & StyleFlags::isFlashing) styleData.setFlags(styleData.flags ^ StyleFlags::isFlashing);

    if (isInvulnerable == v) return;

    if (v) {
        damageReduction = 0.0;
        physicsData.setAbsorbtionFactor(0.0);
    } else {
        damageReduction = 1.0;
        physicsData.setAbsorbtionFactor(definition->absorbtionFactor);
    }
    isInvulnerable = v;
}

void TankBody::receiveDamage(LivingEntity* source, double amount) {
    if (amount > 0 && damageReduction != 0) {
        if (game->tick != lastDamageTick && styleData.opacity < 1) {
            styleData.setOpacity(styleData.opacity + visibilityRateDamage);
        }
        if (styleData.opacity > 1) styleData.setOpacity(1);
    }
    LivingEntity::receiveDamage(source, amount);
}

void TankBody::calculateStatData() {
    CameraData* cd = cameraEntity->getCameraData();

    damagePerTick = cd->statLevels.values[Stat::BodyDamage] + 5 + definition->bodyDamage.value_or(0.0);

    double maxHealthCache = healthData.maxHealth;
    double newMaxHealth = definition->maxHealth
        + 2 * (cd->level - 1)
        + cd->statLevels.values[Stat::MaxHealth] * 20;
    healthData.setMaxHealth(newMaxHealth);

    if (healthData.health == maxHealthCache) {
        healthData.setHealth(healthData.maxHealth);
    } else if (healthData.maxHealth != maxHealthCache) {
        healthData.setHealth(healthData.health * healthData.maxHealth / maxHealthCache);
    }

    regenPerTick = (healthData.maxHealth * 4 * cd->statLevels.values[Stat::HealthRegen] + healthData.maxHealth) / 25000.0;

    reloadTime = 15.0 * std::pow(0.914, cd->statLevels.values[Stat::Reload]);

    cd->setMovementSpeed(
        definition->speed * 2.55
        * std::pow(1.07, cd->statLevels.values[Stat::MovementSpeed])
        / std::pow(1.015, cd->level - 1)
    );

    for (Barrel* barrel : barrels) barrel->calculateStatData();
}

void TankBody::onDeath(LivingEntity* killer) {
    onDeathInner(killer);
    if (onDeathHook) onDeathHook(killer);
}

void TankBody::onDeathInner(LivingEntity* killer) {
    Client* client = cameraEntity->getClient();
    if (!client) { cameraEntity->delete_(); return; }
    if (cameraEntity->getCameraData()->player != this) return;

    cameraEntity->spectatee = ObjectEntity::isObject(killer) ? static_cast<ObjectEntity*>(killer) : nullptr;

    NameData* nd = killer->getNameData();
    cameraEntity->getCameraData()->setKilledBy(nd ? nd->name : "");
}

void TankBody::destroy(bool animate) {
    if (!animate && Entity::exists(cameraEntity)) {
        CameraData* cd = cameraEntity->getCameraData();
        if (cd->player == this) {
            cd->setDeathTick(game->tick);
            double lvl = cd->level;
            double respawn = std::min(std::max(lvl - 1, 1.0), std::floor(std::sqrt(lvl) * 3.2796));
            cd->setRespawnLevel(respawn);
        }
        barrels.clear();
        for (Addon* a : addons) delete a;
        addons.clear();
    }
    LivingEntity::destroy(animate);
}

void TankBody::delete_() {
    if (Entity::exists(cameraEntity)) {
        CameraData* cd = cameraEntity->getCameraData();
        if (cd->player == this) cd->setFOV(0.4);
    }
    LivingEntity::delete_();
}

void TankBody::tick(int tick) {
    positionData.setAngle(
        std::atan2(inputs->mouse.y - positionData.y, inputs->mouse.x - positionData.x)
    );

    if (isInvulnerable) {
        if (game->clients.size() != 1 || !game->arena->isOpen()) {
            Client* client = cameraEntity->getClient();
            if (client && client->accessLevel < config::AccessLevel::FullAccess) setInvulnerability(false);
        }
    }

    if (deletionAnimation || inputs->deleted) regenPerTick = 0;

    LivingEntity::tick(tick);

    if (deletionAnimation) return;

    if (inputs->deleted) {
        if (!Entity::exists(cameraEntity)) { destroy(); return; }
        CameraData* cd = cameraEntity->getCameraData();
        if (cd->level <= 5) { destroy(); return; }
        lastDamageTick = tick;
        healthData.setHealth(healthData.health - 2 - healthData.maxHealth / 500.0);
        if (isInvulnerable) setInvulnerability(false);
        if (styleData.flags & StyleFlags::isFlashing) {
            styleData.setFlags(styleData.flags ^ StyleFlags::isFlashing);
            damageReduction = 1.0;
        }
        return;
    }

    CameraData* cd = cameraEntity->getCameraData();

    if (definition->flags.zoomAbility && (inputs->flags & InputFlags::rightclick)) {
        if (!(cd->flags & CameraFlags::usesCameraCoords)) {
            double angle = std::atan2(inputs->mouse.y - positionData.y, inputs->mouse.x - positionData.x);
            cd->setCameraX(std::cos(angle) * 1500 + positionData.x);
            cd->setCameraY(std::sin(angle) * 1500 + positionData.y);
            cd->setFlags(cd->flags | CameraFlags::usesCameraCoords);
        }
    } else if (cd->flags & CameraFlags::usesCameraCoords) {
        cd->setFlags(cd->flags ^ CameraFlags::usesCameraCoords);
    }

    if (definition->flags.invisibility) {
        if (inputs->flags & InputFlags::leftclick) styleData.setOpacity(styleData.opacity + definition->visibilityRateShooting);
        if (inputs->flags & (InputFlags::up | InputFlags::down | InputFlags::left | InputFlags::right)
            || inputs->movement.x || inputs->movement.y) {
            styleData.setOpacity(styleData.opacity + definition->visibilityRateMoving);
        }
        styleData.setOpacity(styleData.opacity - definition->invisibilityRate);
        styleData.setOpacity(util::constrain(styleData.opacity, 0, 1));
    }

    if ((styleData.flags & StyleFlags::isFlashing)
        && (game->tick >= cd->spawnTick + 374 || inputs->attemptingShot() || inputs->movement.magnitude() > 0)) {
        styleData.setFlags(styleData.flags ^ StyleFlags::isFlashing);
        damageReduction = 1.0;
    }

    if (definition->sides == 2) {
        physicsData.setWidth(physicsData.size * definition->widthRatio.value_or(1.0));
        if (definition->flags.displayAsTrapezoid) physicsData.setFlags(physicsData.flags | PhysicsFlags::isTrapezoid);
    } else if (definition->flags.displayAsStar) {
        styleData.setFlags(styleData.flags | StyleFlags::isStar);
    }

    velocity.x += inputs->movement.x * cd->movementSpeed;
    velocity.y += inputs->movement.y * cd->movementSpeed;
    inputs->movement.x = 0;
    inputs->movement.y = 0;
}
