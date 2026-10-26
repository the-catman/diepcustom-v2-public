#pragma once

#include "Native/Arena.h"
#include "Misc/ShapeManager.h"

class Client;

class SandboxShapeManager : public ShapeManager {
public:
    explicit SandboxShapeManager(ArenaEntity* arena) : ShapeManager(arena) {}

protected:
    int wantedShapes() const override;
};

class SandboxArena : public ArenaEntity {
public:
    static constexpr const char* GAMEMODE_ID = "sandbox";

    explicit SandboxArena(GameServer* game);

    void setSandboxArenaSize(int playerCount);
    void tick(int tick) override;
};
