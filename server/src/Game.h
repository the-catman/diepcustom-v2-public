#pragma once

#include <functional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <utility>

#include "Coder/Writer.h"

class GameServer;
class Client;
class ArenaEntity;
class EntityManager;

using ArenaFactory = std::function<ArenaEntity*(GameServer*)>;

// Writer that broadcasts to every socket in the game.
class WSSWriterStream : public Writer {
public:
    explicit WSSWriterStream(GameServer* game) : game(game) {}
    void send() override;
private:
    GameServer* game;
};

// Set<Client> that keeps globalPlayerCount and clientsAwaitingSpawn in sync.
class ClientSet {
public:
    explicit ClientSet(GameServer* game) : game(game) {}
    void add(Client* client);
    bool erase(Client* client);
    void clear();
    size_t size() const { return inner.size(); }
    auto begin() { return inner.begin(); }
    auto end() { return inner.end(); }
private:
    GameServer* game;
    std::vector<Client*> inner; // insertion-ordered
};

class GameServer {
public:
    static int globalPlayerCount;
    bool running = true;
    std::string gamemode;
    std::string name;
    // The server id in party links: 4 letters.
    std::string id;
    // Set on a private instance (sandbox): the party code that joins it.
    std::string partyCode;
    bool playersOnMap = false;
    ClientSet clients;
    std::vector<std::pair<Client*, std::string>> clientsAwaitingSpawn; // insertion-ordered
    EntityManager* entities = nullptr;
    int tick = 0;
    ArenaEntity* arena = nullptr;
    // Disconnected clients pending free; uWS .close can fire reentrantly mid-tick.
    std::vector<Client*> deadClients;

    GameServer(ArenaFactory arenaFactory, const std::string& gamemode, const std::string& name);
    ~GameServer();

    // Every running game, and the private instances among them: one per link-less sandbox
    // join, dropped once empty.
    static std::vector<GameServer*> games;
    static std::vector<GameServer*> instances;

    WSSWriterStream broadcast() { return WSSWriterStream(this); }
    // The player count is global, so every game hears each change.
    static void broadcastPlayerCount();
    void broadcastMessage(const std::string& text, uint32_t color = 0x000000, double time = 5000, const std::string& id = "");

    void end();
    void onEnd();
    void start();
    void tickLoop();
    void flushDeadClients();

    // Tick-approximated setTimeout; fires from tickLoop.
    void scheduleTimeout(double ms, std::function<void()> fn);

private:
    ArenaFactory arenaFactory;
    std::vector<std::pair<int, std::function<void()>>> timeouts;
};
