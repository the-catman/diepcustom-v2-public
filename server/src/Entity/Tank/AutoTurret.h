#pragma once

#include "Entity/Object.h"
#include "Entity/Live.h"
#include "Native/FieldGroups.h"
#include "Native/Ref.h"
#include "Entity/AI.h"
#include "Const/TankDefinitions.h"

class Barrel;

extern const BarrelDefinition AutoTurretDefinition;

class AutoTurret : public ObjectEntity {
public:
    NameData nameData;

    BarrelBase* owner;
    Barrel* turret;
    AI* ai;
    bool influencedByOwnerInputs = false;
    double baseSize;
    BarrelDefinition turretDefinitionStorage;

    // Set by createAutoTurrets for grouped turrets; idle snaps to slotAngle + rotator angle.
    double slotAngle = 0;
    Ref<ObjectEntity> rotator;

    AutoTurret(BarrelBase* owner, const BarrelDefinition* turretDefinition = &AutoTurretDefinition, double baseSize = 25);
    ~AutoTurret() override { delete ai; }

    NameData* getNameData() override { return &nameData; }

    void onKill(LivingEntity* killedEntity, LivingEntity* weapon) override;
    void tick(int tick) override;
};
