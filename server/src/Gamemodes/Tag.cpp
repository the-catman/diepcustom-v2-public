#include "Gamemodes/Tag.h"
#include "Entity/Misc/TeamEntity.h"
#include "Entity/Tank/TankBody.h"
#include "Native/Entity.h"
#include "Client.h"
#include "Game.h"
#include "Util.h"
#include "Config.h"
#include "Const/Enums.h"

#include <algorithm>
#include <cmath>

static const int TEAM_COLORS[] = { Color::TeamBlue, Color::TeamRed, Color::TeamPurple, Color::TeamGreen };
static constexpr int TEAM_COUNT = 4;
static constexpr int MIN_PLAYERS = TEAM_COUNT * 1;
static constexpr double ARENA_SIZE = 11150;
static constexpr double SHRINK_AMOUNT = 100;
static constexpr double MIN_SIZE = 6600;
static constexpr int SHRINK_INTERVAL = (int)(15 * config::tps);

int TagShapeManager::wantedShapes() const {
    double size = (arena->width + arena->height) / 2;
    int ratio = (int)std::ceil(std::pow(size / 2500.0, 2));
    return (int)std::floor(12.5 * ratio);
}

TagArena::TagArena(GameServer* game) : ArenaEntity(game) {
    delete shapes;
    shapes = new TagShapeManager(this);

    shapeScoreRewardMultiplier = 3.0;
    arenaData.setFlags(arenaData.flags | ArenaFlags::hiddenScores);

    std::vector<int> teamOrder(TEAM_COLORS, TEAM_COLORS + TEAM_COUNT);
    util::shuffleArray(teamOrder);

    for (int color : teamOrder) {
        TeamEntity* team = new TeamEntity(game, color);
        teams.push_back(team);
    }

    updateBounds(ARENA_SIZE * 2, ARENA_SIZE * 2);
}

TeamEntity* TagArena::decideTeam(Client* client) {
    auto it = playerTeamMap.find(client);
    if (it != playerTeamMap.end()) return it->second;

    // If not enough players to start, pick team with fewest players; otherwise pick team with most players
    std::vector<TankBody*> alive = getAlivePlayers();
    TeamEntity* team = (int)alive.size() <= MIN_PLAYERS ? teams.back() : teams.front();
    playerTeamMap[client] = team;
    return team;
}

void TagArena::spawnPlayer(TankBody* tank, Client* client) {
    TankBody* t = tank;
    auto* arena = this;
    tank->onDeathHook = [t, client, arena](LivingEntity* killer) {
        if ((int)arena->game->clients.size() < MIN_PLAYERS) return;

        Entity* teamEnt = ((ObjectEntity*)t)->relationsData.team;
        Entity* killerTeamEnt = ((ObjectEntity*)killer)->relationsData.team;

        bool playerIsATeam = false;
        for (TeamEntity* te : arena->teams) {
            if (te == teamEnt) { playerIsATeam = true; break; }
        }
        if (!playerIsATeam) return;

        bool killerTeamIsATeam = false;
        for (TeamEntity* te : arena->teams) {
            if (te == killerTeamEnt) { killerTeamIsATeam = true; break; }
        }
        if (killerTeamIsATeam) arena->playerTeamMap[client] = static_cast<TeamEntity*>(killerTeamEnt);
        else arena->playerTeamMap[client] = static_cast<TeamEntity*>(teamEnt);
    };

    TeamEntity* team = decideTeam(client);
    TeamEntity::setTeam(team, tank);

    updateTeamScores();

    if (attemptFactorySpawn(tank)) return;

    VectorAbstract pos = findPlayerSpawnLocation();
    ((ObjectEntity*)tank)->positionData.x = pos.x;
    ((ObjectEntity*)tank)->positionData.y = pos.y;
}

double TagArena::getTeamScore(TeamEntity* team) const {
    auto it = teamScoreMap.find(team);
    return it != teamScoreMap.end() ? it->second : 0.0;
}

void TagArena::updateTeamScores() {
    for (TeamEntity* team : teams) {
        teamScoreMap[team] = (double)getTeamPlayers(team).size();
    }
    std::stable_sort(teams.begin(), teams.end(), [this](TeamEntity* t1, TeamEntity* t2) {
        return getTeamScore(t2) < getTeamScore(t1);
    });
}

void TagArena::updateTagScoreboard() {
    int length = (int)std::min((int)teams.size(), 10);
    for (int i = 0; i < length; ++i) {
        TeamEntity* team = teams[i];
        double count = getTeamScore(team);
        arenaData.scoreboardColors.set(i, team->teamData.teamColor);
        arenaData.scoreboardNames.set(i, team->teamName);
        arenaData.scoreboardTanks.set(i, -1);
        arenaData.scoreboardScores.set(i, count);
        arenaData.scoreboardSuffixes.set(i, count == 1.0 ? " player" : " players");
    }
    arenaData.setScoreboardAmount(std::min(10, length));
}

void TagArena::updateArenaState() {
    updateTeamScores();

    std::vector<TankBody*> alive = getAlivePlayers();
    int arenaPlayerCount = (int)alive.size();
    TeamEntity* leaderTeam = teams[0];

    int leaderCount = (int)getTeamPlayers(leaderTeam).size();
    if (leaderCount == arenaPlayerCount && arenaPlayerCount >= MIN_PLAYERS) {
        if (state == ArenaState::OPEN) {
            game->broadcastMessage(
                leaderTeam->teamName + " HAS WON THE GAME!",
                colorHex(leaderTeam->teamData.teamColor),
                -1
            );
            state = ArenaState::OVER;
            GameServer* g = game;
            game->scheduleTimeout(5000, [g]() { g->arena->close(); });
        }
    }

    if (arenaPlayerCount == 0 && state == ArenaState::CLOSING) {
        state = ArenaState::CLOSED;
        GameServer* g = game;
        game->scheduleTimeout(10000, [g]() { g->end(); });
        return;
    }

    if (((int)(game->tick % (int)config::scoreboardUpdateInterval)) == 0) {
        updateTagScoreboard();
    }
}

void TagArena::tick(int tick) {
    ArenaEntity::tick(tick);

    if ((tick % SHRINK_INTERVAL) == 0 && width > MIN_SIZE) {
        updateBounds(width - SHRINK_AMOUNT, height - SHRINK_AMOUNT);
    }
}
