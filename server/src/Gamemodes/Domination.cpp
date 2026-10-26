#include "Gamemodes/Domination.h"
#include "Entity/Misc/TeamEntity.h"
#include "Entity/Misc/TeamBase.h"
#include "Entity/Misc/Dominator.h"
#include "Entity/Tank/TankBody.h"
#include "Native/Entity.h"
#include "Client.h"
#include "Game.h"
#include "Util.h"
#include "Const/Enums.h"

#include <algorithm>
#include <cmath>

static constexpr double ARENA_SIZE = 11150;
static constexpr double BASE_SIZE = ARENA_SIZE / (3.0 + 1.0/3.0); // ~3345
static constexpr double DOM_BASE_SIZE = BASE_SIZE / 2;
static constexpr bool ENABLE_SCOREBOARD = false;
static const int TEAM_COLORS[] = { Color::TeamBlue, Color::TeamRed };
static constexpr int TEAM_COUNT = 2;

DominationArena::DominationArena(GameServer* game) : ArenaEntity(game) {
    shapeScoreRewardMultiplier = 2.0;
    updateBounds(ARENA_SIZE * 2, ARENA_SIZE * 2);
    arenaData.setFlags(arenaData.flags | ArenaFlags::hiddenScores);

    double flipLeft  = util::randomFloat() > 0.5 ? 1.0 : -1.0;
    double flipRight = util::randomFloat() > 0.5 ? -1.0 : 1.0;

    for (int i = 0; i < TEAM_COUNT; ++i) {
        int teamColor = TEAM_COLORS[i];
        TeamEntity* team = new TeamEntity(game, teamColor);
        double side = (i % 2 != 0) ? 1.0 : -1.0;

        double x = side * ARENA_SIZE - side * BASE_SIZE / 2;
        double y = side * (side == 1.0 ? flipLeft : flipRight) * (ARENA_SIZE - BASE_SIZE / 2);

        flipLeft  *= side;
        flipRight *= -side;

        new TeamBase(game, team, x, y, BASE_SIZE, BASE_SIZE);
        teams.push_back(team);
        partyTeams.push_back(team);
    }

    TeamBase* seb = new TeamBase(game, this,  ARENA_SIZE / 2.5,  ARENA_SIZE / 2.5, DOM_BASE_SIZE, DOM_BASE_SIZE, false);
    TeamBase* swb = new TeamBase(game, this, -ARENA_SIZE / 2.5,  ARENA_SIZE / 2.5, DOM_BASE_SIZE, DOM_BASE_SIZE, false);
    TeamBase* nwb = new TeamBase(game, this, -ARENA_SIZE / 2.5, -ARENA_SIZE / 2.5, DOM_BASE_SIZE, DOM_BASE_SIZE, false);
    TeamBase* neb = new TeamBase(game, this,  ARENA_SIZE / 2.5, -ARENA_SIZE / 2.5, DOM_BASE_SIZE, DOM_BASE_SIZE, false);

    Dominator* SE = new Dominator(this, seb);
    Dominator* SW = new Dominator(this, swb);
    Dominator* NW = new Dominator(this, nwb);
    Dominator* NE = new Dominator(this, neb);

    SE->prefix = "SE ";
    SW->prefix = "SW ";
    NW->prefix = "NW ";
    NE->prefix = "NE ";

    dominators.push_back(SE);
    dominators.push_back(SW);
    dominators.push_back(NW);
    dominators.push_back(NE);
}

TeamEntity* DominationArena::decideTeam(Client* client) {
    auto it = playerTeamMap.find(client);
    if (it != playerTeamMap.end()) return it->second;

    TeamEntity* team = util::randomFrom(teams);
    playerTeamMap[client] = team;
    return team;
}

void DominationArena::spawnPlayer(TankBody* tank, Client* client) {
    TeamEntity* team = decideTeam(client);
    TeamEntity::setTeam(team, tank);

    if (attemptFactorySpawn(tank)) return;

    TeamBase* base = team->base;
    if (!base) { ArenaEntity::spawnPlayer(tank, client); return; }

    VectorAbstract pos = util::getRandomPosition(base);
    ((ObjectEntity*)tank)->positionData.x = pos.x;
    ((ObjectEntity*)tank)->positionData.y = pos.y;
}

int DominationArena::getTeamDominatorCount(TeamEntity* team) const {
    int count = 0;
    for (Dominator* dom : dominators) {
        if (!dom) continue; // Ref resolves null once a dominator is freed
        if (((ObjectEntity*)dom)->relationsData.team == team) count++;
    }
    return count;
}

void DominationArena::updateDomScoreboard() {
    std::vector<Dominator*> live;
    for (Dominator* dom : dominators) if (dom) live.push_back(dom);
    std::stable_sort(live.begin(), live.end(), [](Dominator* d1, Dominator* d2) {
        return ((LivingEntity*)d2)->healthData.health < ((LivingEntity*)d1)->healthData.health;
    });

    int length = (int)std::min((int)live.size(), 10);
    for (int i = 0; i < length; ++i) {
        Dominator* dom = live[i];
        int col = ((ObjectEntity*)dom)->styleData.color;
        arenaData.scoreboardColors.set(i, col == Color::Tank ? Color::ScoreboardBar : col);
        arenaData.scoreboardNames.set(i, dom->prefix.empty() ? ((TankBody*)dom)->nameData.name : dom->prefix);
        arenaData.scoreboardTanks.set(i, -1);
        arenaData.scoreboardScores.set(i, ((LivingEntity*)dom)->healthData.health);
        arenaData.scoreboardSuffixes.set(i, " HP");
    }
    arenaData.setScoreboardAmount(length);
}

void DominationArena::updateArenaState() {
    if (ENABLE_SCOREBOARD) updateDomScoreboard();

    int domCount = (int)dominators.size();
    for (TeamEntity* team : teams) {
        if (getTeamDominatorCount(team) == domCount) {
            if (state == ArenaState::OPEN) {
                game->broadcastMessage(
                    team->teamName + " HAS WON THE GAME!",
                    colorHex(team->teamData.teamColor),
                    -1
                );
                state = ArenaState::OVER;
                GameServer* g = game;
                game->scheduleTimeout(5000, [g]() { g->arena->close(); });
            }
        }
    }

    // compact dead dominators (swap-pop)
    for (int i = (int)dominators.size() - 1; i >= 0; --i) {
        if (!Entity::exists(dominators[i])) {
            Dominator* pop = dominators.back();
            dominators.pop_back();
            if (i < (int)dominators.size()) dominators[i] = pop;
        }
    }

    if (state == ArenaState::CLOSING && getAlivePlayers().empty()) {
        state = ArenaState::CLOSED;
        GameServer* g = game;
        game->scheduleTimeout(10000, [g]() { g->end(); });
    }
}
