#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_set>
#include <vector>

#include "Coder/Writer.h"
#include "Entity/AI.h"
#include "Config.h"
#include "Native/Ref.h"

class GameServer;
class ClientCamera;
class Client;

extern std::unordered_set<std::string> bannedClients;

// Borrows the client's scratch buffer for its lifetime so per-tick packets reuse capacity.
class WSWriterStream : public Writer {
public:
    explicit WSWriterStream(Client* client);
    ~WSWriterStream() override;
    void send() override;
private:
    Client* client;
};

// Inputs that buffer flags and apply them once a tick.
class ClientInputs : public Inputs {
public:
    uint32_t cachedFlags = 0;
    // Left stick from the last gamepad input (flag 512), at most length 1.
    VectorAbstract gamepad{0, 0};
    bool isPossessing = false;
    Client* client;
    explicit ClientInputs(Client* client) : client(client) {}
};

class Client {
public:
    bool terminated = false;
    int accessLevel = config::AccessLevel::NoAccess;
    std::shared_ptr<ClientInputs> inputs;
    GameServer* game;
    Ref<ClientCamera> camera;
    bool isInvulnerable = false;

    bool connected = true;
    int kickAtTick = -1;
    std::string ipAddress;
    std::function<void(const uint8_t*, size_t)> sendRaw;
    std::function<void()> closeWs;
    std::vector<uint8_t> writeScratch;
    // Achievement condition keys earned this tick, flushed as one packet.
    std::vector<std::string> pendingAchievements;
    // Set by any cheat and never cleared for the connection: no more achievements.
    bool achievementsBlocked = false;

    explicit Client(GameServer* game);

    WSWriterStream write() { return WSWriterStream(this); }
    void acceptClient();
    void send(const uint8_t* data, size_t len);
    void onClose();
    void terminate();
    void onMessage(const uint8_t* data, size_t len, bool isBinary);

    void setHasCheated(bool value);
    bool hasCheated() const;
    bool possess(AI* ai);
    void notify(const std::string& text, uint32_t color = 0x000000, double time = 5000, const std::string& id = "");
    void ban();
    void createAndSpawnPlayer(const std::string& name);
    void tick(int tick);
    std::string toString(bool verbose = false) const;

private:
    int connectTick;
    int lastPingTick;
    std::array<std::vector<std::vector<uint8_t>>, 256> incomingCache;
    bool devCheatsUsed = false;

    void handleIncoming(int header, const std::vector<uint8_t>& data);
    void switchGame(GameServer* target);
    void parsePacket(int header, const std::vector<uint8_t>& data);
};
