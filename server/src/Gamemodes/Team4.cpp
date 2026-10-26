#include "Gamemodes/Team4.h"
#include "Entity/Misc/TeamEntity.h"
#include "Entity/Misc/TeamBase.h"
#include "Entity/Tank/TankBody.h"
#include "Client.h"
#include "Game.h"
#include "Util.h"
#include "Const/Enums.h"

static constexpr double ARENA_SIZE = 11150;
static constexpr double BASE_SIZE = ARENA_SIZE / (3.0 + 1.0/3.0); // ~3345

Teams4Arena::Teams4Arena(GameServer* game) : ArenaEntity(game) {
    updateBounds(ARENA_SIZE * 2, ARENA_SIZE * 2);

    blueTeamEntity   = new TeamEntity(game, Color::TeamBlue);
    redTeamEntity    = new TeamEntity(game, Color::TeamRed);
    purpleTeamEntity = new TeamEntity(game, Color::TeamPurple);
    greenTeamEntity  = new TeamEntity(game, Color::TeamGreen);
    partyTeams = {blueTeamEntity, redTeamEntity, purpleTeamEntity, greenTeamEntity};

    new TeamBase(game, blueTeamEntity,   -ARENA_SIZE + BASE_SIZE / 2, -ARENA_SIZE + BASE_SIZE / 2, BASE_SIZE, BASE_SIZE);
    new TeamBase(game, redTeamEntity,     ARENA_SIZE - BASE_SIZE / 2,  ARENA_SIZE - BASE_SIZE / 2, BASE_SIZE, BASE_SIZE);
    new TeamBase(game, purpleTeamEntity,  ARENA_SIZE - BASE_SIZE / 2, -ARENA_SIZE + BASE_SIZE / 2, BASE_SIZE, BASE_SIZE);
    new TeamBase(game, greenTeamEntity,  -ARENA_SIZE + BASE_SIZE / 2,  ARENA_SIZE - BASE_SIZE / 2, BASE_SIZE, BASE_SIZE);
}

TeamEntity* Teams4Arena::decideTeam(Client* client) {
    auto it = playerTeamMap.find(client);
    if (it != playerTeamMap.end()) return it->second;

    TeamEntity* team = util::randomFrom(std::vector<TeamEntity*>{blueTeamEntity, redTeamEntity, purpleTeamEntity, greenTeamEntity});
    playerTeamMap[client] = team;
    return team;
}

void Teams4Arena::spawnPlayer(TankBody* tank, Client* client) {
    TeamEntity* team = decideTeam(client);
    TeamEntity::setTeam(team, tank);

    if (attemptFactorySpawn(tank)) return;

    TeamBase* base = team->base;
    if (!base) { ArenaEntity::spawnPlayer(tank, client); return; }

    VectorAbstract pos = util::getRandomPosition(base);
    ((ObjectEntity*)tank)->positionData.setX(pos.x);
    ((ObjectEntity*)tank)->positionData.setY(pos.y);
}
