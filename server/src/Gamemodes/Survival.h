#pragma once

#include "Native/Arena.h"
#include "Misc/ShapeManager.h"

class SurvivalShapeManager : public ShapeManager {
public:
    explicit SurvivalShapeManager(ArenaEntity* arena) : ShapeManager(arena) {}

protected:
    int wantedShapes() const override;
};

class SurvivalArena : public ArenaEntity {
public:
    static constexpr const char* GAMEMODE_ID = "survival";

    explicit SurvivalArena(GameServer* game);

    void setSurvivalArenaSize(int playerCount);
    void updateArenaState();
    void manageCountdown();
    void onGameStarted() override;
    void tick(int tick) override;
};
