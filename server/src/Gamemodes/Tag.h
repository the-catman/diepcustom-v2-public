#pragma once

#include <unordered_map>
#include <vector>

#include "Native/Arena.h"
#include "Misc/ShapeManager.h"

class TeamEntity;
class Client;
class TankBody;

class TagShapeManager : public ShapeManager {
public:
    explicit TagShapeManager(ArenaEntity* arena) : ShapeManager(arena) {}

protected:
    int wantedShapes() const override;
};

class TagArena : public ArenaEntity {
public:
    static constexpr const char* GAMEMODE_ID = "tag";

    std::vector<TeamEntity*> teams;
    std::unordered_map<TeamEntity*, double> teamScoreMap;

    explicit TagArena(GameServer* game);

    TeamEntity* decideTeam(Client* client) override;
    void spawnPlayer(TankBody* tank, Client* client) override;

    double getTeamScore(TeamEntity* team) const;
    void updateTeamScores();
    void updateTagScoreboard();
    void updateArenaState();
    void tick(int tick) override;
};
