#include "Gamemodes/Mothership.h"
#include "Entity/Misc/TeamEntity.h"
#include "Entity/Misc/Mothership.h"
#include "Entity/Tank/TankBody.h"
#include "Native/Entity.h"
#include "Client.h"
#include "Game.h"
#include "Util.h"
#include "Const/Enums.h"

#include <algorithm>
#include <cmath>

static constexpr double ARENA_SIZE = 11150;
static const int TEAM_COLORS[] = { Color::TeamBlue, Color::TeamRed };
static constexpr int TEAM_COUNT = 2;

MothershipArena::MothershipArena(GameServer* game) : ArenaEntity(game) {
    updateBounds(ARENA_SIZE * 2, ARENA_SIZE * 2);
    shapeScoreRewardMultiplier = 3.0;
    arenaData.setFlags(arenaData.flags | ArenaFlags::hiddenScores);

    double randAngle = util::randomFloat() * util::PI2;
    for (int i = 0; i < TEAM_COUNT; ++i) {
        TeamEntity* team = new TeamEntity(game, TEAM_COLORS[i]);
        teams.push_back(team);
        partyTeams.push_back(team);

        Mothership* mot = new Mothership(game);
        motherships.push_back(mot);

        ((ObjectEntity*)mot)->relationsData.team = team;
        ((ObjectEntity*)mot)->styleData.color = team->teamData.teamColor;
        ((ObjectEntity*)mot)->positionData.x = std::cos(randAngle) * ARENA_SIZE * 0.8;
        ((ObjectEntity*)mot)->positionData.y = std::sin(randAngle) * ARENA_SIZE * 0.8;

        randAngle += util::PI2 / TEAM_COUNT;
    }
}

TeamEntity* MothershipArena::decideTeam(Client* client) {
    auto it = playerTeamMap.find(client);
    if (it != playerTeamMap.end()) return it->second;

    TeamEntity* team = util::randomFrom(teams);
    playerTeamMap[client] = team;
    return team;
}

void MothershipArena::spawnPlayer(TankBody* tank, Client* client) {
    TeamEntity* team = decideTeam(client);
    TeamEntity::setTeam(team, tank);

    if (attemptFactorySpawn(tank)) return;

    VectorAbstract pos = findPlayerSpawnLocation();
    ((ObjectEntity*)tank)->positionData.x = pos.x;
    ((ObjectEntity*)tank)->positionData.y = pos.y;
}

void MothershipArena::updateMothershipScoreboard() {
    std::stable_sort(motherships.begin(), motherships.end(), [](Mothership* m1, Mothership* m2) {
        return ((LivingEntity*)m2)->healthData.health < ((LivingEntity*)m1)->healthData.health;
    });

    int length = (int)std::min((int)motherships.size(), 10);
    for (int i = 0; i < length; ++i) {
        Mothership* mot = motherships[i];
        Entity* teamEnt = ((ObjectEntity*)mot)->relationsData.team;
        bool isTeam = TeamEntity::isTeamEntity(teamEnt);

        int col = ((ObjectEntity*)mot)->styleData.color;
        arenaData.scoreboardColors.set(i, col == Color::Tank ? Color::ScoreboardBar : col);
        arenaData.scoreboardNames.set(i, isTeam ? static_cast<TeamEntity*>(teamEnt)->teamName : ("Mothership " + std::to_string(i + 1)));
        arenaData.scoreboardTanks.set(i, -1);
        arenaData.scoreboardScores.set(i, ((LivingEntity*)mot)->healthData.health);
        arenaData.scoreboardSuffixes.set(i, " HP");
    }
    arenaData.setScoreboardAmount(length);
}

void MothershipArena::updateArenaState() {
    // compact dead motherships (swap-pop, backwards)
    for (int i = (int)motherships.size() - 1; i >= 0; --i) {
        if (!Entity::exists(motherships[i])) {
            Mothership* pop = motherships.back();
            motherships.pop_back();
            if (i < (int)motherships.size()) motherships[i] = pop;
        }
    }

    if ((int)motherships.size() <= 1 && state == ArenaState::OPEN) {
        state = ArenaState::OVER;
        GameServer* g = game;
        game->scheduleTimeout(5000, [g]() { g->arena->close(); });
    }

    if (getAlivePlayers().empty() && state == ArenaState::CLOSING) {
        state = ArenaState::CLOSED;
        GameServer* g = game;
        game->scheduleTimeout(10000, [g]() { g->end(); });
        return;
    }

    updateMothershipScoreboard();
}
