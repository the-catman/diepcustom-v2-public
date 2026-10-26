#pragma once

#include <vector>

#include "Entity/Object.h"
#include "Native/FieldGroups.h"
#include "Const/TankDefinitions.h"

class BarrelAddon;

class ShootCycle {
public:
    double pos;
    double reloadTime;

    explicit ShootCycle(class Barrel* barrel);
    void tick();

private:
    class Barrel* barrelEntity;
};

class Barrel : public ObjectEntity {
public:
    BarrelDefinition* definition;
    Ref<BarrelBase> tank;
    ShootCycle* shootCycle;
    bool attemptingShot = false;
    double bulletAccel = 20;
    int droneCount = 0;
    std::vector<BarrelAddon*> addons;
    BarrelData barrelData;

    Barrel(BarrelBase* owner, BarrelDefinition* barrelDefinition);
    ~Barrel() override;

    BarrelData* getBarrelData() override { return &barrelData; }

    void shoot();
    void calculateStatData();
    void tick(int tick) override;
};
