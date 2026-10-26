#pragma once

#include <vector>

#include "Entity/Object.h"
#include "Native/FieldGroups.h"

// Base for all entities with health.
class LivingEntity : public ObjectEntity {
public:
    HealthData healthData;

    double scoreReward = 0;
    double damageReduction = 1.0;
    double minDamageMultiplier = 1.0;
    double maxDamageMultiplier = 4.0;
    double opacityGainOnDamage = 0.0;
    double damagePerTick = 8;
    double regenPerTick = 0;

    explicit LivingEntity(GameServer* game);

    HealthData* getHealthData() override { return &healthData; }

    void destroy(bool animate = true) override;

    static bool isLive(Entity* entity) { return ObjectEntity::isObject(entity) && entity->getHealthData() != nullptr; }
    static void handleCollision(LivingEntity* a, LivingEntity* b);

    virtual void receiveDamage(LivingEntity* source, double amount);
    virtual void onKill(LivingEntity* entity, LivingEntity* weapon) {}
    virtual void onDeath(LivingEntity* killer) {}

    void applyPhysics() override;
    void tick(int tick) override;

protected:
    std::vector<LivingEntity*> damagedEntities;
    int lastDamageTick = -1;
    int lastDamageAnimationTick = -1;
};
