#pragma once

#include <unordered_map>
#include <vector>

#include "Native/Arena.h"

class TeamEntity;
class Dominator;
class Client;
class TankBody;

class DominationArena : public ArenaEntity {
public:
    static constexpr const char* GAMEMODE_ID = "dom";

    std::vector<Ref<Dominator>> dominators;
    std::vector<TeamEntity*> teams;

    explicit DominationArena(GameServer* game);

    TeamEntity* decideTeam(Client* client) override;
    void spawnPlayer(TankBody* tank, Client* client) override;
    int getTeamDominatorCount(TeamEntity* team) const;
    void updateDomScoreboard();
    void updateArenaState();
};
