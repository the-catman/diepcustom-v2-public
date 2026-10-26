#include "Game.h"
#include "Client.h"
#include "Native/Manager.h"
#include "Native/Arena.h"
#include "Const/Enums.h"
#include "Config.h"
#include "Util.h"

#include <algorithm>

int GameServer::globalPlayerCount = 0;
std::vector<GameServer*> GameServer::games;
std::vector<GameServer*> GameServer::instances;

void WSSWriterStream::send() {
    for (Client* client : game->clients) client->send(data(), size());
}

void ClientSet::add(Client* client) {
    if (std::find(inner.begin(), inner.end(), client) != inner.end()) return;
    GameServer::globalPlayerCount += 1;
    GameServer::broadcastPlayerCount();
    inner.push_back(client);
}

bool ClientSet::erase(Client* client) {
    auto it = std::find(inner.begin(), inner.end(), client);
    bool success = it != inner.end();
    if (success) {
        inner.erase(it);
        GameServer::globalPlayerCount -= 1;
        GameServer::broadcastPlayerCount();
        auto& q = game->clientsAwaitingSpawn;
        q.erase(std::remove_if(q.begin(), q.end(), [&](auto& p) { return p.first == client; }), q.end());
    }
    return success;
}

void ClientSet::clear() {
    GameServer::globalPlayerCount -= (int)inner.size();
    GameServer::broadcastPlayerCount();
    game->clientsAwaitingSpawn.clear();
    inner.clear();
}

GameServer::GameServer(ArenaFactory arenaFactory, const std::string& gamemode, const std::string& name)
    : gamemode(gamemode), name(name), clients(this), arenaFactory(std::move(arenaFactory)) {
    entities = new EntityManager(this);
    tick = 0;
    arena = this->arenaFactory(this);
    games.push_back(this);
}

GameServer::~GameServer() {
    games.erase(std::find(games.begin(), games.end(), this));
    entities->clear();
    delete entities;
}

void GameServer::broadcastPlayerCount() {
    for (GameServer* game : games) game->broadcast().u8(ClientBound::PlayerCount).vu(globalPlayerCount).send();
}

void GameServer::broadcastMessage(const std::string& text, uint32_t color, double time, const std::string& id) {
    broadcast().u8(ClientBound::Notification).stringNT(text).u32(color).floatLE((float)time).stringNT(id).send();
}

void GameServer::end() {
    util::saveToLog("Game Instance Ending", "Game running " + gamemode + " is now closing.", 0xee4132);
    tick = 0;
    timeouts.clear();
    entities->clear();
    running = false;
    onEnd();
}

void GameServer::onEnd() {
    start();
}

void GameServer::start() {
    if (running) return;
    running = true;
    util::log("New game instance booting up");

    // Clear camera handles before freeing the manager they point into.
    for (Client* client : clients) client->camera = nullptr;

    delete entities;
    entities = new EntityManager(this);
    tick = 0;
    arena = arenaFactory(this);

    for (Client* client : clients) client->acceptClient();
}

void GameServer::scheduleTimeout(double ms, std::function<void()> fn) {
    int fireTick = tick + std::max(1, (int)(ms / config::mspt + 0.5));
    timeouts.emplace_back(fireTick, std::move(fn));
}

void GameServer::tickLoop() {
    tick += 1;

    // Entity creation throws OOEI when the table fills; contain it to this tick.
    try {
        for (size_t i = 0; i < timeouts.size();) {
            if (timeouts[i].first <= tick) {
                auto fn = std::move(timeouts[i].second);
                timeouts[i] = std::move(timeouts.back());
                timeouts.pop_back();
                fn();
            } else {
                ++i;
            }
        }

        entities->preTick(tick);
        // Snapshot: a client's tick may terminate it, erasing from clients mid-iteration.
        std::vector<Client*> clientsNow(clients.begin(), clients.end());
        for (Client* client : clientsNow) client->tick(tick);
        entities->tick(tick);
        entities->postTick(tick);
    } catch (const std::exception& e) {
        util::log("Tick aborted: ", e.what());
    }
    entities->reclaim();
    flushDeadClients();
}

void GameServer::flushDeadClients() {
    if (deadClients.empty()) return;
    for (Client* c : deadClients) delete c;
    deadClients.clear();
}
