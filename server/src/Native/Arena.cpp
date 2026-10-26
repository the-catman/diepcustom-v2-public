#include "Native/Arena.h"
#include "Native/Manager.h"
#include "Native/Camera.h"
#include "Misc/ShapeManager.h"
#include "Misc/BossManager.h"
#include "Entity/Tank/TankBody.h"
#include "Entity/Tank/Barrel.h"
#include "Entity/Misc/ArenaCloser.h"
#include "Entity/Boss/AbstractBoss.h"
#include "Entity/Misc/TeamEntity.h"
#include "Physics/CollisionManager.h"
#include "Client.h"
#include "Game.h"
#include "Util.h"
#include "Config.h"
#include "Const/Enums.h"

#include <algorithm>
#include <cmath>

ArenaEntity::ArenaEntity(GameServer* game) : Entity(game), arenaData(this), teamData(this) {
    // Publish before building sub-entities: their ctors read game->arena.
    game->arena = this;
    bossManager = new BossManager(this);
    shapes = new ShapeManager(this);

    updateBounds(width = 22300, height = 22300);

    arenaData.topY = -height / 2;
    arenaData.bottomY = height / 2;
    arenaData.leftX = -width / 2;
    arenaData.rightX = width / 2;

    arenaData.flags = ArenaFlags::gameReadyStart;
    arenaData.playersNeeded = 0;
    arenaData.ticksUntilStart = config::countdownDuration;

    teamData.teamColor = Color::Neutral;
}

ArenaEntity::~ArenaEntity() {
    delete bossManager;
    delete shapes;
}

VectorAbstract ArenaEntity::findSpawnLocation(double w, double h) {
    VectorAbstract pos{(double)(int)(util::randomFloat() * w - w / 2), (double)(int)(util::randomFloat() * h - h / 2)};

    for (int i = 0; i < 20; ++i) {
        if (!isValidSpawnLocation(pos.x, pos.y)) {
            pos.x = (double)(int)(util::randomFloat() * w - w / 2);
            pos.y = (double)(int)(util::randomFloat() * h - h / 2);
            continue;
        }

        ObjectEntity* entity = game->entities->collisionManager->getFirstMatch(pos.x, pos.y, 1000, 1000, [&](ObjectEntity* e) {
            if (!(TankBody::isTank(e) || AbstractBoss::isBoss(e))) return false;
            double dX = e->positionData.x - pos.x;
            double dY = e->positionData.y - pos.y;
            return (dX * dX + dY * dY) < 1000000.0;
        });

        if (entity) {
            pos.x = (double)(int)(util::randomFloat() * w - w / 2);
            pos.y = (double)(int)(util::randomFloat() * h - h / 2);
            continue;
        }
        break;
    }
    return pos;
}

VectorAbstract ArenaEntity::findPlayerSpawnLocation() {
    VectorAbstract pos = findSpawnLocation();
    for (int i = 0; i < 20; ++i) {
        if (std::max(pos.x, pos.y) < arenaData.rightX / 2 && std::min(pos.x, pos.y) > arenaData.leftX / 2) {
            pos = findSpawnLocation();
            continue;
        }
        break;
    }
    return pos;
}

void ArenaEntity::updateScoreboard(std::vector<TankBody*>& scoreboardPlayers) {
    double scoreboardCount = (arenaData.flags & ArenaFlags::hiddenScores) ? 0 : std::min((int)scoreboardPlayers.size(), 10);
    arenaData.setScoreboardAmount(scoreboardCount);

    if (!scoreboardCount) {
        if (arenaData.flags & ArenaFlags::showsLeaderArrow) arenaData.setFlags(arenaData.flags ^ ArenaFlags::showsLeaderArrow);
        return;
    }

    std::stable_sort(scoreboardPlayers.begin(), scoreboardPlayers.end(), [](TankBody* p1, TankBody* p2) {
        return p2->scoreData.score < p1->scoreData.score;
    });
    leader = scoreboardPlayers[0];

    arenaData.setFlags(arenaData.flags | ArenaFlags::showsLeaderArrow);
    for (int i = 0; i < (int)scoreboardCount; ++i) {
        TankBody* player = scoreboardPlayers[i];
        if (player->styleData.color == Color::Tank) arenaData.scoreboardColors.set(i, Color::ScoreboardBar);
        else arenaData.scoreboardColors.set(i, player->styleData.color);
        arenaData.scoreboardNames.set(i, player->nameData.name);
        arenaData.scoreboardScores.set(i, player->scoreData.score);
        arenaData.scoreboardTanks.set(i, player->currentTank);
    }
}

void ArenaEntity::updateArenaState() {
    if (((int)(game->tick % (int)config::scoreboardUpdateInterval)) != 0) return;

    std::vector<TankBody*> players = getAlivePlayers();
    updateScoreboard(players);

    if (players.empty() && state == ArenaState::CLOSING) {
        state = ArenaState::CLOSED;
        GameServer* g = game;
        game->scheduleTimeout(10000, [g]() { g->end(); });
        return;
    }
}

void ArenaEntity::manageCountdown() {
    bool isReady = arenaData.flags & ArenaFlags::gameReadyStart;
    if (isReady) arenaData.setTicksUntilStart(arenaData.ticksUntilStart - 1);

    if (state == ArenaState::COUNTDOWN && isReady && arenaData.ticksUntilStart < 0) onGameStarted();

    std::vector<std::pair<Client*, std::string>> awaiting(game->clientsAwaitingSpawn.begin(), game->clientsAwaitingSpawn.end());
    for (auto& [client, name] : awaiting) {
        ClientCamera* camera = client->camera;
        if (!Entity::exists(camera)) continue;

        if (state == ArenaState::COUNTDOWN) {
            camera->cameraData.setFlags(CameraFlags::gameWaitingStart);
            continue;
        }

        client->createAndSpawnPlayer(name);

        if (camera->cameraData.flags & CameraFlags::gameWaitingStart) {
            camera->cameraData.flags &= ~CameraFlags::gameWaitingStart;
        }

        auto& q = game->clientsAwaitingSpawn;
        q.erase(std::remove_if(q.begin(), q.end(), [&](auto& p) { return p.first == client; }), q.end());
    }
}

std::vector<TankBody*> ArenaEntity::getAlivePlayers() {
    std::vector<TankBody*> players;
    for (Client* client : game->clients) {
        if (!client->camera) continue;
        Entity* entity = client->camera->cameraData.player;
        if (Entity::exists(entity) && TankBody::isTank(entity)) players.push_back((TankBody*)entity);
    }
    return players;
}

std::vector<TankBody*> ArenaEntity::getTeamPlayers(Entity* team) {
    std::vector<TankBody*> players = getAlivePlayers();
    std::vector<TankBody*> teamPlayers;
    for (TankBody* entity : players) {
        if (((ObjectEntity*)entity)->relationsData.team == team) teamPlayers.push_back(entity);
    }
    return teamPlayers;
}

void ArenaEntity::updateBounds(double arenaWidth, double arenaHeight) {
    width = arenaWidth;
    height = arenaHeight;
    arenaData.setTopY(-arenaHeight / 2);
    arenaData.setBottomY(arenaHeight / 2);
    arenaData.setLeftX(-arenaWidth / 2);
    arenaData.setRightX(arenaWidth / 2);
}

std::string ArenaEntity::partyCode(Client* client) {
    if (partyTeams.empty()) return "";
    TeamEntity* team = decideTeam(client);
    return team ? team->partyCode : "";
}

bool ArenaEntity::joinParty(Client* client, const std::string& code) {
    for (TeamEntity* team : partyTeams) {
        if (team->partyCode != code) continue;
        playerTeamMap[client] = team;
        return true;
    }
    return false;
}

void ArenaEntity::spawnPlayer(TankBody* tank, Client* client) {
    (void)client;
    if (attemptFactorySpawn(tank)) return;
    VectorAbstract loc = findPlayerSpawnLocation();
    ((ObjectEntity*)tank)->positionData.x = loc.x;
    ((ObjectEntity*)tank)->positionData.y = loc.y;
}

bool ArenaEntity::attemptFactorySpawn(TankBody* tank) {
    if (util::randomFloat() > config::factorySpawnChance) return false;

    Entity* team = ((ObjectEntity*)tank)->relationsData.team;
    if (!TeamEntity::isTeam(team)) return false;

    std::vector<TankBody*> teammates = getTeamPlayers(team);
    std::vector<TankBody*> factories;
    for (TankBody* teammate : teammates) {
        if (teammate->currentTank == Tank::Factory && !((ObjectEntity*)teammate)->deletionAnimation) factories.push_back(teammate);
    }
    if (factories.empty()) return false;

    TankBody* factory = util::randomFrom(factories);
    Vector wp = ((ObjectEntity*)factory)->getWorldPosition();
    Barrel* barrel = factory->barrels[0];
    double shootAngle = barrel->definition->angle + ((ObjectEntity*)factory)->positionData.angle;

    ((ObjectEntity*)tank)->positionData.x = wp.x + (std::cos(shootAngle) * barrel->physicsData.size) - std::sin(shootAngle) * barrel->definition->offset * factory->scaleFactor;
    ((ObjectEntity*)tank)->positionData.y = wp.y + (std::sin(shootAngle) * barrel->physicsData.size) + std::cos(shootAngle) * barrel->definition->offset * factory->scaleFactor;
    ((ObjectEntity*)tank)->addVelocity(shootAngle, 25);
    return true;
}

void ArenaEntity::close() {
    for (Client* client : game->clients) client->notify("Arena closed: No players can join", 0xff0000, -1);

    state = ArenaState::CLOSING;
    arenaData.setFlags(arenaData.flags | ArenaFlags::noJoining);

    int acCount = (int)std::floor(std::sqrt(width) / 10);
    double radius = width * 0.7071067811865476 + 5000;
    for (int i = 0; i < acCount; ++i) {
        ArenaCloser* ac = new ArenaCloser(game);
        double angle = ((double)i / acCount) * util::PI2;
        ac->positionData.x = std::cos(angle) * radius;
        ac->positionData.y = std::sin(angle) * radius;
        ac->positionData.angle = angle + M_PI;
    }

    util::saveToLog("Arena Closing", "Arena running at `" + game->gamemode + "` is now closing.", 0xffe869);
}

void ArenaEntity::onGameStarted() {
    state = ArenaState::OPEN;
}

void ArenaEntity::tick(int tick) {
    shapes->tick();
    updateArenaState();
    manageCountdown();

    if (leader && arenaData.flags & ArenaFlags::showsLeaderArrow) {
        arenaData.setLeaderX(((ObjectEntity*)leader)->positionData.x);
        arenaData.setLeaderY(((ObjectEntity*)leader)->positionData.y);
    }

    if (bossManager) bossManager->tick(tick);
}
