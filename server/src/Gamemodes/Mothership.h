#pragma once

#include <unordered_map>
#include <vector>

#include "Native/Arena.h"

class TeamEntity;
class Mothership;
class Client;
class TankBody;

class MothershipArena : public ArenaEntity {
public:
    static constexpr const char* GAMEMODE_ID = "mot";

    std::vector<TeamEntity*> teams;
    std::vector<Ref<Mothership>> motherships;

    explicit MothershipArena(GameServer* game);

    TeamEntity* decideTeam(Client* client) override;
    void spawnPlayer(TankBody* tank, Client* client) override;
    void updateMothershipScoreboard();
    void updateArenaState();
};
