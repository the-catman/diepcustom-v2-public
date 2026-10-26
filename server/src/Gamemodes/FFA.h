#pragma once

#include "Native/Arena.h"

class FFAArena : public ArenaEntity {
public:
    static constexpr const char* GAMEMODE_ID = "ffa";

    explicit FFAArena(GameServer* game) : ArenaEntity(game) {}
};
