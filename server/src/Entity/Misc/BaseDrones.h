#pragma once

#include "Entity/Object.h"
#include "Const/TankDefinitions.h"

class TeamBase;
class Barrel;

// ObjectEntity already carries cameraEntity, reloadTime, inputs (BarrelBase fields).
class BaseDrones : public ObjectEntity {
public:
    BaseDrones(TeamBase* base, int droneCount = 12);

private:
    BarrelDefinition droneSpawnerDef;
    Barrel* droneSpawner;
};
