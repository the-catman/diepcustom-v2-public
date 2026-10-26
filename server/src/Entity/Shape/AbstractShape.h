#pragma once

#include "Entity/Live.h"
#include "Native/FieldGroups.h"
#include "Entity/AI.h"

class AbstractShape : public LivingEntity {
public:
    static constexpr double BASE_ROTATION = AI::PASSIVE_ROTATION;
    static constexpr double BASE_ORBIT    = 0.005;
    static constexpr double BASE_VELOCITY = 1.0;

    NameData nameData;
    bool isShiny = false;

    AbstractShape(GameServer* game,
                  double baseRotation = BASE_ROTATION,
                  double baseOrbit    = BASE_ORBIT,
                  double baseVelocity = BASE_VELOCITY);

    NameData* getNameData() override { return &nameData; }

    static bool isShape(Entity* e) { return e && (e->entityTags & EntityTags::isShape); }

    void tick(int tick) override;

protected:
    bool doIdleRotate = true;
    double orbitAngle;
    double orbitRate;
    double rotationRate;
    double shapeVelocity;

    int isTurning = 0;
    double targetTurningAngle = 0;

    void turnTo(double angle);
};
