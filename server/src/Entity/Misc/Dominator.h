#pragma once

#include <string>
#include <vector>

#include "Entity/Tank/TankBody.h"
#include "Entity/AI.h"
#include "Const/Enums.h"

class ArenaEntity;
class TeamBase;

class Dominator : public TankBody {
public:
    static constexpr double SIZE = 160;
    static const std::vector<int> DOMINATOR_CLASSES;

    AI* ai;
    TeamBase* base;
    std::string prefix = "";

    Dominator(ArenaEntity* arena, TeamBase* base, int tankId = -1);
    ~Dominator() override { delete ai; }

    static bool isDominator(Entity* e) { return e && (e->entityTags & EntityTags::isDominator); }

    void onDeath(LivingEntity* killer) override;
    void tick(int tick) override;

private:
    TankDefinition localDef; // mutable copy so we can zero out speed
};
