#include "Entity/Live.h"
#include "Game.h"
#include "Const/Enums.h"

#include <algorithm>
#include <cmath>

LivingEntity::LivingEntity(GameServer* game) : ObjectEntity(game), healthData(this) {}

void LivingEntity::destroy(bool animate) {
    if (hash == 0) return;
    if (animate) healthData.setHealth(0);
    ObjectEntity::destroy(animate);
}

void LivingEntity::handleCollision(LivingEntity* e1, LivingEntity* e2) {
    if (e1->relationsData.team.rawHash() != 0 && e1->relationsData.team.sameAs(e2->relationsData.team)) return;
    if (e1->healthData.health <= 0 || e2->healthData.health <= 0) return;

    bool d1HasE2 = std::find(e1->damagedEntities.begin(), e1->damagedEntities.end(), e2) != e1->damagedEntities.end();
    bool d2HasE1 = std::find(e2->damagedEntities.begin(), e2->damagedEntities.end(), e1) != e2->damagedEntities.end();
    if (d1HasE2 || d2HasE1) return;
    if (e1->damageReduction == 0 && e2->damageReduction == 0) return;
    if ((e1->damagePerTick == 0 && e1->physicsData.pushFactor == 0) || (e2->damagePerTick == 0 && e2->physicsData.pushFactor == 0)) return;

    double common = std::max(e2->minDamageMultiplier, e1->minDamageMultiplier);
    common *= std::min(e2->maxDamageMultiplier, e1->maxDamageMultiplier);
    double dF1 = (e1->damagePerTick * common) * e2->damageReduction;
    double dF2 = (e2->damagePerTick * common) * e1->damageReduction;

    double ratio = std::max(1 - e1->healthData.health / dF2, 1 - e2->healthData.health / dF1);
    double damage1to2 = dF1 * std::min(1.0, 1 - ratio);
    double damage2to1 = dF2 * std::min(1.0, 1 - ratio);

    e1->receiveDamage(e2, damage2to1);
    e2->receiveDamage(e1, damage1to2);
}

void LivingEntity::receiveDamage(LivingEntity* source, double amount) {
    if (healthData.health <= 0.0001) {
        healthData.setHealth(0);
        return;
    }

    damagedEntities.push_back(source);

    if (lastDamageAnimationTick != game->tick && !(styleData.flags & StyleFlags::hasNoDmgIndicator)) {
        styleData.setFlags(styleData.flags ^ StyleFlags::hasBeenDamaged);
        lastDamageAnimationTick = game->tick;
    }

    lastDamageTick = game->tick;
    healthData.setHealth(healthData.health - amount);

    if (healthData.health <= 0.0001) {
        healthData.setHealth(0);

        ObjectEntity* killer = source;
        while (true) {
            Entity* o = killer->relationsData.owner.get();
            if (!ObjectEntity::isObject(o) || o->hash == 0) break;
            killer = (ObjectEntity*)o;
        }

        if (LivingEntity::isLive(killer)) onDeath((LivingEntity*)killer);
        source->onKill(this, source);
    }
}

void LivingEntity::applyPhysics() {
    ObjectEntity::applyPhysics();

    if (healthData.health <= 0) {
        destroy(true);
        damagedEntities.clear();
        return;
    }

    if (healthData.health < healthData.maxHealth) {
        healthData.setHealth(healthData.health + regenPerTick);
        if (game->tick - lastDamageTick >= 750) {
            healthData.setHealth(healthData.health + healthData.maxHealth / 250);
        }
    }

    if (healthData.health > healthData.maxHealth) healthData.setHealth(healthData.maxHealth);

    damagedEntities.clear();
}

void LivingEntity::tick(int tick) {
    ObjectEntity::tick(tick);
}
