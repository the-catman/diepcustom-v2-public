#pragma once

#include <functional>
#include <vector>

#include "Entity/Live.h"
#include "Native/FieldGroups.h"
#include "Native/Camera.h"
#include "Entity/AI.h"
#include "Const/TankDefinitions.h"

class Barrel;
class Addon;

class TankBody : public LivingEntity {
public:
    NameData nameData;
    ScoreData scoreData;

    std::vector<Barrel*> barrels;
    bool isInvulnerable = false;

    double baseSize = 50;
    TankDefinition* definition = nullptr;
    int currentTank;

    // per-instance death hook (gamemodes attach to it)
    std::function<void(LivingEntity*)> onDeathHook;

    TankBody(GameServer* game, CameraEntity* cameraEntity, std::shared_ptr<Inputs> inputs);
    ~TankBody() override;

    NameData* getNameData() override { return &nameData; }
    ScoreData* getScoreData() override { return &scoreData; }

    static bool isTank(Entity* e) { return e && (e->entityTags & EntityTags::isTank); }

    // Score/notify/kill-packet part of onKill; also runs posthumously via Bullet::onKill.
    static void grantKillRewards(CameraEntity* cam, LivingEntity* entity, LivingEntity* self, LivingEntity* weapon);

    void setTank(int id);
    void calculateStatData();
    void setInvulnerability(bool v);
    void receiveDamage(LivingEntity* source, double amount) override;

    void onKill(LivingEntity* entity, LivingEntity* weapon) override;
    void onDeath(LivingEntity* killer) override;
    void onDeathInner(LivingEntity* killer);
    void destroy(bool animate = true) override;
    void delete_() override;
    void tick(int tick) override;

private:
    std::vector<Addon*> addons;
};
