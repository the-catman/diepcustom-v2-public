#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "Game.h"
#include "Match.h"
#include "Native/Ref.h"

class Client;
class Entity;
class Reader;

namespace live {

namespace ToServer {
    constexpr int BotsCreate = 0x20;
    constexpr int BotsReady = 0x21;
    constexpr int BotsRematch = 0x22;
    constexpr int BotsLeave = 0x23;
}

namespace ToClient {
    constexpr uint8_t BotsLists = 0x20;
    constexpr uint8_t BotsRoom = 0x21;
    constexpr uint8_t BotsResult = 0x22;
}

// The rooms of the bots mode: their seats, party codes, countdowns and matches. Everyone outside a
// match waits in one shared lobby game.
class Rooms {
public:
    static constexpr int MAX_ROOMS = 8;
    static constexpr int COUNTDOWN = 75;
    // Ticks without the brain before a match ends in a draw.
    static constexpr int OFFLINE_LIMIT = 250;

    enum Phase { None, Waiting, Countdown, Playing, Over };
    enum Reason { Eliminated, Time, Left, Offline };

    explicit Rooms(Bots& bots);
    ~Rooms();

    // The lobby's arena, for the mode that makes the lobby.
    ArenaFactory lobbyArena();
    void open(GameServer* lobby);
    void tick();
    void onClose(Client* client);
    // Seats a guest whose Init carried this party code.
    bool join(Client* client, const std::string& code);

    size_t size() const { return rooms.size(); }

private:
    struct Seat {
        // 0 for a Player, k for bot k-1.
        int who = 0;
        int tank = 0;
        std::string name;
        Client* client = nullptr;
        std::optional<std::array<int, 8>> build;
        bool ready = false;
        bool rematch = false;
        // Sent the room's party code since its last Accept.
        bool coded = false;
        bool alive = false;
        // Its human left during the match.
        bool left = false;
        int diedAt = -1;
        // The team of the human's tank, for spectating once it dies.
        Ref<Entity> team;
    };

    struct Room {
        std::string code;
        Phase phase = Waiting;
        bool deterministic = false;
        int n0 = 1, n1 = 1;
        std::vector<Seat> seats;
        int countdown = 0;
        int ticks = 0;
        std::unique_ptr<Match> match;
        Result result;
        int reason = Eliminated;
        int offline = 0;
        bool online = true;
        bool unfit = false;
        bool changed = true;
        int sinceSent = 0;
    };

    Bots& bots;
    GameServer* lobby = nullptr;
    std::vector<std::unique_ptr<Room>> rooms;
    // Left a match; moved to the lobby on the next tick.
    std::vector<Client*> returning;
    // Lobby clients and the lists they were last sent.
    std::unordered_map<Client*, uint64_t> listed;

    bool onPacket(Client* client, int header, Reader& r);
    void create(Client* client, Reader& r);
    void ready(Client* client, Reader& r);
    void voteRematch(Client* client);
    void leave(Client* client);

    void step(Room& room, bool online);
    void start(Room& room);
    void play(Room& room, bool online);
    void finish(Room& room, const Result& result, int reason);
    void rematch(Room& room);
    void dropMatch(Room& room);
    void vacate(Room& room, int seat, bool closed);
    void spectate(Room& room);

    std::pair<Room*, int> find(Client* client);
    bool allReady(const Room& room) const;
    bool hasBots(const Room& room) const;
    bool hasHumans(const Room& room) const;
    bool fits(const Room& room) const;
    bool canStart(Room& room);
    int reasonFor(const Room& room, const Result& result) const;
    std::string newCode() const;
    uint64_t listsKey() const;

    void toLobby(Client* client);
    void publish(Room& room);
    void notifyAll(const Room& room, const std::string& text);
    void sendLists(Client* client);
    void sendRoom(Client* client, const Room* room);
    void sendResult(Client* client, const Room& room);
    void sendCode(Client* client, const std::string& code);
};

}
