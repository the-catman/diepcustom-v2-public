#pragma once

#include <string>
#include <vector>

#include "Const/TankDefinitions.h"

#include "Entity/Live.h"
#include "Native/FieldGroups.h"
#include "Entity/AI.h"

class Barrel;
class LivingEntity;

enum class BossMoveTarget { None = -1, BottomRight = 0, TopRight = 1, TopLeft = 2, BottomLeft = 3 };

class BossMovementControl {
public:
    BossMoveTarget target = BossMoveTarget::None;

    explicit BossMovementControl(class AbstractBoss* boss) : boss(boss) {}
    void moveBoss();

private:
    class AbstractBoss* boss;
};

class AbstractBoss : public LivingEntity {
public:
    NameData nameData;
    std::string altName = "";
    AI* ai;
    std::shared_ptr<Inputs> inputs;

    double movementSpeed = 0.5;

    static bool isBoss(Entity* e) { return e && (e->entityTags & EntityTags::isBoss); }

    explicit AbstractBoss(GameServer* game);
    ~AbstractBoss() override { delete ai; }

    NameData* getNameData() override { return &nameData; }

    void onDeath(LivingEntity* killer) override;
    void tick(int tick) override;

protected:
    std::vector<Barrel*> barrels;
    // Barrel definitions owned by this boss (for boss-specific configs).
    std::vector<BarrelDefinition> ownedBarrelDefs;
    BossMovementControl movementControl;

    virtual void moveAroundMap();

private:
    bool hasBeenWelcomed = false;
};
