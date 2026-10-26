#pragma once

#include <unordered_map>

#include "Native/Arena.h"

class TeamEntity;
class TeamBase;
class Client;
class TankBody;

class Teams4Arena : public ArenaEntity {
public:
    static constexpr const char* GAMEMODE_ID = "4teams";

    TeamEntity* blueTeamEntity;
    TeamEntity* redTeamEntity;
    TeamEntity* greenTeamEntity;
    TeamEntity* purpleTeamEntity;

    explicit Teams4Arena(GameServer* game);

    TeamEntity* decideTeam(Client* client) override;
    void spawnPlayer(TankBody* tank, Client* client) override;
};
