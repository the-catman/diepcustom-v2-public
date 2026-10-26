#pragma once

#include <unordered_map>
#include <vector>

#include "Native/Arena.h"

class TeamEntity;
class TeamBase;
class Client;
class TankBody;

class Teams2Arena : public ArenaEntity {
public:
    static constexpr const char* GAMEMODE_ID = "teams";

    TeamEntity* blueTeamEntity;
    TeamEntity* redTeamEntity;

    explicit Teams2Arena(GameServer* game);

    TeamEntity* decideTeam(Client* client) override;
    void spawnPlayer(TankBody* tank, Client* client) override;
};
