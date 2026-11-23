#include "Rooms.h"

#include <algorithm>
#include <cmath>

#include "Client.h"
#include "Coder/Reader.h"
#include "Config.h"
#include "Const/Enums.h"
#include "Const/TankDefinitions.h"
#include "Entity/Tank/TankBody.h"
#include "Misc/BossManager.h"
#include "Misc/ShapeManager.h"
#include "Native/Arena.h"
#include "Native/Camera.h"
#include "Native/Manager.h"
#include "Util.h"

namespace live {

namespace {

constexpr double LOBBY_SIZE = 4000;
constexpr size_t NAME_LENGTH = 16;
const int SECOND = (int)config::tps;

class NoShapes : public ShapeManager {
public:
    explicit NoShapes(ArenaEntity* arena) : ShapeManager(arena) {}

protected:
    int wantedShapes() const override { return 0; }
};

class LobbyArena : public ArenaEntity {
public:
    LobbyArena(GameServer* game, Rooms& rooms) : ArenaEntity(game), rooms(rooms) {
        shapes = std::make_unique<NoShapes>(this);
        bossManager.reset();
        updateBounds(LOBBY_SIZE, LOBBY_SIZE);
        state = ArenaState::OPEN;
    }

    bool joinParty(Client* client, const std::string& code) override { return rooms.join(client, code); }

private:
    Rooms& rooms;
};

std::string cleanName(const std::string& name) { return util::truncateUtf8(util::trim(name), NAME_LENGTH); }

bool offers(const std::vector<std::array<int, 9>>& tanks, int64_t tank) {
    return std::any_of(tanks.begin(), tanks.end(), [&](const std::array<int, 9>& t) { return t[0] == tank; });
}

using Build = std::optional<std::array<int, 8>>;

// A 1 and 8 stats in server order, or a 0 for the tank's usual build.
Build readBuild(Reader& r) {
    if (!r.u8()) return std::nullopt;
    std::array<int, 8> build;
    for (int& x : build) x = r.u8();
    return build;
}

// Within the tank's caps and a max level tank's points.
bool buildFits(int tank, const std::array<int, 8>& build) {
    const TankDefinition* def = getTankById(tank);
    if (!def) return false;
    int points = 0;
    for (int i = 0; i < 8; ++i) {
        int max = i < (int)def->stats.size() ? (int)def->stats[i].max : 0;
        if (build[i] > max) return false;
        points += build[i];
    }
    return points <= CameraEntity::calculateStatCount(config::maxPlayerLevel);
}

bool living(Entity* e) {
    return TankBody::isTank(e) && !((TankBody*)e)->deletionAnimation && ((TankBody*)e)->healthData.health > 0;
}

}

Rooms::Rooms(Bots& bots) : bots(bots) {}

Rooms::~Rooms() {
    for (auto& room : rooms) dropMatch(*room);
}

ArenaFactory Rooms::lobbyArena() {
    return [this](GameServer* game) { return (ArenaEntity*)new LobbyArena(game, *this); };
}

void Rooms::open(GameServer* game) {
    lobby = game;
    lobby->onPacket = [this](Client* c, int header, Reader& r) { return onPacket(c, header, r); };
}

// Packets arrive inside a game's tickLoop, so they only change room state; tick() moves clients.
bool Rooms::onPacket(Client* client, int header, Reader& r) {
    switch (header) {
        case ServerBound::Spawn:
        case ServerBound::StatUpgrade:
        case ServerBound::TankUpgrade:
        case ServerBound::TakeTank:
        case ServerBound::TCPInit:
            return true;
        case ToServer::BotsCreate: create(client, r); return true;
        case ToServer::BotsReady: ready(client, r); return true;
        case ToServer::BotsRematch: voteRematch(client); return true;
        case ToServer::BotsLeave: leave(client); return true;
        default: return false;
    }
}

void Rooms::create(Client* client, Reader& r) {
    int flags = r.u8();
    int n0 = r.u8(), n1 = r.u8();
    if (n0 < 1 || n0 > 2 || n1 < 1 || n1 > 2) return client->notify("Each side has one or two tanks");
    std::vector<std::pair<uint32_t, uint32_t>> picks(n0 + n1);
    for (auto& [who, tank] : picks) {
        who = r.vu();
        tank = r.vu();
    }
    std::string name = cleanName(r.stringNT());
    Build build = readBuild(r);

    if (client->game != lobby || find(client).first) return client->notify("You're already in a match");
    if (!bots.online()) return client->notify("Bots are offline");
    if ((int)rooms.size() >= MAX_ROOMS) return client->notify("Every room is in use, try again soon");

    const Lists& lists = bots.lists();
    auto room = std::make_unique<Room>();
    room->deterministic = flags & 1;
    room->n0 = n0;
    room->n1 = n1;
    room->seats.resize(picks.size());
    int you = -1;
    for (size_t i = 0; i < picks.size(); ++i) {
        auto [who, tank] = picks[i];
        Seat& seat = room->seats[i];
        if (who == 0) {
            if (!offers(lists.playerTanks, tank)) return client->notify("That tank isn't available");
            if (you < 0) you = (int)i;
        } else {
            if (who > lists.bots.size()) return client->notify("That bot isn't available");
            const Lists::Bot& bot = lists.bots[who - 1];
            if (!offers(bot.tanks, tank)) return client->notify(bot.name + " doesn't play that tank");
            seat.name = bot.name;
            seat.ready = true;
        }
        seat.who = (int)who;
        seat.tank = (int)tank;
    }
    if (you < 0) return client->notify("Take a seat for yourself");
    if (build && !buildFits(room->seats[you].tank, *build)) return client->notify("That build doesn't fit your tank");

    Seat& creator = room->seats[you];
    creator.client = client;
    creator.name = name;
    creator.build = build;
    creator.ready = true;
    room->code = newCode();
    room->online = bots.online();
    rooms.push_back(std::move(room));
}

void Rooms::ready(Client* client, Reader& r) {
    std::string name = cleanName(r.stringNT());
    Build build = readBuild(r);
    auto [room, i] = find(client);
    if (!room || room->phase > Countdown) return;
    Seat& seat = room->seats[i];
    if (build && !buildFits(seat.tank, *build)) return client->notify("That build doesn't fit your tank");
    seat.name = name;
    seat.build = build;
    seat.ready = true;
    room->changed = true;
}

void Rooms::voteRematch(Client* client) {
    auto [room, i] = find(client);
    if (!room || room->phase != Over || room->seats[i].rematch) return;
    room->seats[i].rematch = true;
    room->changed = true;
}

void Rooms::leave(Client* client) {
    auto [room, i] = find(client);
    if (room) vacate(*room, i, false);
    else if (client->game == lobby) sendRoom(client, nullptr);
}

bool Rooms::join(Client* client, const std::string& code) {
    if (find(client).first) return false;
    for (auto& room : rooms) {
        if (room->code != code) continue;
        if (room->phase != Waiting) return false;
        for (Seat& seat : room->seats) {
            if (seat.who != 0 || seat.client) continue;
            seat.client = client;
            seat.ready = false;
            seat.coded = false;
            seat.name.clear();
            room->changed = true;
            return true;
        }
        return false;
    }
    return false;
}

void Rooms::onClose(Client* client) {
    listed.erase(client);
    returning.erase(std::remove(returning.begin(), returning.end(), client), returning.end());
    auto [room, i] = find(client);
    if (room) vacate(*room, i, true);
}

void Rooms::vacate(Room& room, int i, bool closed) {
    Seat& seat = room.seats[i];
    Client* client = seat.client;
    seat.client = nullptr;
    seat.build.reset();
    seat.ready = false;
    seat.rematch = false;
    seat.coded = false;
    if (room.phase == Playing) seat.left = true;
    // The result still names whoever played the seat.
    if (room.phase < Playing) seat.name.clear();
    room.changed = true;
    if (closed) return;
    if (client->game != lobby) {
        returning.push_back(client);
    } else {
        sendRoom(client, nullptr);
        sendCode(client, "");
    }
}

void Rooms::tick() {
    bots.poll();

    std::vector<Client*> back;
    back.swap(returning);
    for (Client* client : back) {
        toLobby(client);
        sendRoom(client, nullptr);
    }

    for (size_t i = 0; i < rooms.size();) {
        if (hasHumans(*rooms[i])) { ++i; continue; }
        dropMatch(*rooms[i]);
        rooms.erase(rooms.begin() + i);
    }

    uint64_t key = listsKey();
    for (Client* client : lobby->clients) {
        if (!Entity::exists(client->camera)) continue;
        auto it = listed.find(client);
        if (it == listed.end() || it->second != key) sendLists(client);
    }

    bool online = bots.online();
    for (auto& room : rooms) step(*room, online);

    bots.flush();
}

void Rooms::step(Room& room, bool online) {
    if (room.online != online) {
        room.online = online;
        if (!online && hasBots(room)) notifyAll(room, "Bots went offline");
    }

    switch (room.phase) {
        case Waiting:
            if (allReady(room) && canStart(room)) {
                room.phase = Countdown;
                room.countdown = COUNTDOWN;
                room.changed = true;
            }
            break;
        case Countdown:
            if (!allReady(room) || !canStart(room)) {
                room.phase = Waiting;
                room.changed = true;
            } else if (--room.countdown <= 0) {
                start(room);
            }
            break;
        case Playing:
            play(room, online);
            break;
        case Over: {
            room.match->coast();
            for (size_t i = 0; i < room.seats.size(); ++i) {
                bool alive = room.match->alive((int)i);
                if (alive == room.seats[i].alive) continue;
                room.seats[i].alive = alive;
                room.changed = true;
            }
            spectate(room);
            bool any = false, all = true;
            for (const Seat& seat : room.seats) {
                if (!seat.client) continue;
                any = true;
                all = all && seat.rematch;
            }
            if (any && all) rematch(room);
            break;
        }
        default:
            break;
    }

    if (room.phase == Waiting || room.phase == Countdown) {
        for (Seat& seat : room.seats) {
            if (!seat.client || seat.coded) continue;
            sendCode(seat.client, room.code);
            seat.coded = true;
        }
    }
    publish(room);
}

void Rooms::start(Room& room) {
    Spec spec;
    spec.n0 = room.n0;
    spec.n1 = room.n1;
    spec.deterministic = room.deterministic;
    for (const Seat& seat : room.seats) spec.seats.push_back({seat.who - 1, seat.tank, seat.name, seat.client, seat.build});
    room.match = bots.start(spec);
    room.changed = true;
    if (!room.match) {
        notifyAll(room, "The match couldn't start");
        room.phase = Waiting;
        return;
    }
    room.match->game()->onPacket = [this](Client* c, int header, Reader& r) { return onPacket(c, header, r); };
    room.phase = Playing;
    room.ticks = 0;
    room.offline = 0;
    for (Seat& seat : room.seats) {
        seat.rematch = false;
        seat.left = false;
        seat.alive = true;
        seat.diedAt = -1;
        seat.team = nullptr;
    }
}

void Rooms::play(Room& room, bool online) {
    Match& match = *room.match;
    for (Seat& seat : room.seats) {
        if (!seat.client || !Entity::exists(seat.client->camera)) continue;
        Entity* player = seat.client->camera->cameraData.player;
        if (living(player)) seat.team = ((TankBody*)player)->relationsData.team.get();
    }

    bool ended = match.tick();
    Result result = match.result();
    room.ticks = result.ticks;
    for (size_t i = 0; i < room.seats.size(); ++i) {
        Seat& seat = room.seats[i];
        bool alive = match.alive((int)i);
        if (alive == seat.alive) continue;
        seat.alive = alive;
        if (!alive) seat.diedAt = result.ticks;
        room.changed = true;
    }
    if (ended) return finish(room, result, reasonFor(room, result));

    if (online || !hasBots(room)) {
        room.offline = 0;
    } else if (++room.offline >= OFFLINE_LIMIT) {
        result.winner = -1;
        return finish(room, result, Offline);
    }
    spectate(room);
}

void Rooms::finish(Room& room, const Result& result, int reason) {
    room.result = result;
    room.reason = reason;
    room.ticks = result.ticks;
    room.phase = Over;
    room.changed = true;
    for (const Seat& seat : room.seats) {
        if (seat.client) sendResult(seat.client, room);
    }
}

// The last of the losing side to die decides whether it was beaten or left.
int Rooms::reasonFor(const Room& room, const Result& result) const {
    if (result.winner < 0) return Eliminated;
    int last = -1;
    bool left = false;
    for (size_t i = 0; i < room.seats.size(); ++i) {
        int team = (int)i < room.n0 ? 0 : 1;
        const Seat& seat = room.seats[i];
        if (team == result.winner || seat.diedAt < last) continue;
        left = (seat.diedAt == last && left) || seat.left;
        last = seat.diedAt;
    }
    return left ? Left : Eliminated;
}

void Rooms::rematch(Room& room) {
    dropMatch(room);
    for (Seat& seat : room.seats) {
        seat.rematch = false;
        seat.alive = false;
        seat.coded = false;
        if (seat.who == 0 && !seat.client) seat.name.clear();
    }
    room.phase = allReady(room) && canStart(room) ? Countdown : Waiting;
    room.countdown = COUNTDOWN;
    room.changed = true;
}

// Every client goes back to the lobby before the match's game is freed.
void Rooms::dropMatch(Room& room) {
    if (!room.match) return;
    GameServer* game = room.match->game();
    std::vector<Client*> inside(game->clients.begin(), game->clients.end());
    for (Client* client : inside) {
        toLobby(client);
        auto it = std::find(returning.begin(), returning.end(), client);
        if (it == returning.end()) continue;
        returning.erase(it);
        sendRoom(client, nullptr);
    }
    for (Seat& seat : room.seats) seat.team = nullptr;
    game->flushDeadClients();
    room.match.reset();
}

// A dead human watches a living teammate, else anyone left, once the tank it watched is gone.
void Rooms::spectate(Room& room) {
    GameServer* game = room.match->game();
    std::vector<TankBody*> tanks;
    bool scanned = false;
    for (Seat& seat : room.seats) {
        if (!seat.client || seat.client->game != game) continue;
        ClientCamera* camera = seat.client->camera;
        if (!Entity::exists(camera) || living(camera->cameraData.player) || living(camera->spectatee)) continue;
        if (!scanned) {
            EntityManager& entities = *game->entities;
            for (int id = 0; id <= entities.lastId; ++id) {
                if (living(entities.inner[id])) tanks.push_back((TankBody*)entities.inner[id]);
            }
            scanned = true;
        }
        if (tanks.empty()) return;
        Entity* team = seat.team.get();
        TankBody* pick = tanks[0];
        for (TankBody* tank : tanks) {
            if (team && tank->relationsData.team.get() == team) { pick = tank; break; }
        }
        camera->spectatee = pick;
    }
}

std::pair<Rooms::Room*, int> Rooms::find(Client* client) {
    for (auto& room : rooms) {
        for (size_t i = 0; i < room->seats.size(); ++i) {
            if (room->seats[i].client == client) return {room.get(), (int)i};
        }
    }
    return {nullptr, -1};
}

bool Rooms::allReady(const Room& room) const {
    for (const Seat& seat : room.seats) {
        if (seat.who == 0 && (!seat.client || !seat.ready)) return false;
    }
    return true;
}

bool Rooms::hasBots(const Room& room) const {
    for (const Seat& seat : room.seats) {
        if (seat.who) return true;
    }
    return false;
}

bool Rooms::hasHumans(const Room& room) const {
    for (const Seat& seat : room.seats) {
        if (seat.client) return true;
    }
    return false;
}

// The brain may have restarted with other bots since the room was made.
bool Rooms::fits(const Room& room) const {
    const Lists& lists = bots.lists();
    for (const Seat& seat : room.seats) {
        if (seat.who == 0) {
            if (!offers(lists.playerTanks, seat.tank)) return false;
            continue;
        }
        if (seat.who > (int)lists.bots.size()) return false;
        const Lists::Bot& bot = lists.bots[seat.who - 1];
        if (bot.name != seat.name || !offers(bot.tanks, seat.tank)) return false;
    }
    return true;
}

bool Rooms::canStart(Room& room) {
    if (hasBots(room) && !bots.online()) return false;
    bool fit = fits(room);
    if (!fit && !room.unfit) notifyAll(room, "The bots have changed, set up the match again");
    room.unfit = !fit;
    return fit;
}

std::string Rooms::newCode() const {
    for (;;) {
        std::string code;
        for (int i = 0; i < 4; ++i) code += (char)(int)(util::randomFloat() * 256);
        bool taken = std::any_of(rooms.begin(), rooms.end(), [&](const auto& room) { return room->code == code; });
        if (!taken) return code;
    }
}

uint64_t Rooms::listsKey() const { return (uint64_t)bots.lists().version * 2 + (bots.online() ? 1 : 0); }

void Rooms::toLobby(Client* client) {
    client->moveTo(lobby);
    sendLists(client);
}

void Rooms::publish(Room& room) {
    bool ticking = room.phase == Countdown || room.phase == Playing;
    if (!room.changed && !(ticking && ++room.sinceSent >= SECOND)) return;
    room.changed = false;
    room.sinceSent = 0;
    for (const Seat& seat : room.seats) {
        if (seat.client) sendRoom(seat.client, &room);
    }
}

void Rooms::notifyAll(const Room& room, const std::string& text) {
    for (const Seat& seat : room.seats) {
        if (seat.client) seat.client->notify(text);
    }
}

void Rooms::sendLists(Client* client) {
    const Lists& lists = bots.lists();
    auto w = client->write();
    w.u8(ToClient::BotsLists).u8(bots.online() ? 1 : 0).vu((int32_t)lists.bots.size());
    for (const Lists::Bot& bot : lists.bots) {
        w.stringNT(bot.name).vu((int32_t)bot.tanks.size());
        for (const auto& tank : bot.tanks) w.vu(tank[0]);
    }
    w.vu((int32_t)lists.playerTanks.size());
    for (const auto& tank : lists.playerTanks) {
        w.vu(tank[0]);
        for (int k = 1; k < 9; ++k) w.u8(tank[k]);
    }
    w.send();
    listed[client] = listsKey();
}

void Rooms::sendRoom(Client* client, const Room* room) {
    auto w = client->write();
    w.u8(ToClient::BotsRoom);
    if (!room) {
        w.u8(None).u8(0).u8(0).u8(0).u8(0xff).vu(0).send();
        return;
    }
    int you = 0xff;
    for (size_t i = 0; i < room->seats.size(); ++i) {
        if (room->seats[i].client == client) you = (int)i;
    }
    w.u8(room->phase).u8(room->deterministic ? 1 : 0).u8(room->n0).u8(room->n1).u8(you);
    for (const Seat& seat : room->seats) {
        int state = 0;
        if (seat.who || seat.client) state |= 1;
        if (seat.who || (seat.client && seat.ready)) state |= 2;
        if (room->phase >= Playing && seat.alive) state |= 4;
        if (seat.rematch) state |= 8;
        w.vu(seat.who).vu(seat.tank).u8(state).stringNT(seat.name);
    }
    int ticks = room->phase == Countdown ? room->countdown : room->phase >= Playing ? room->ticks : 0;
    w.vu(ticks).send();
}

void Rooms::sendResult(Client* client, const Room& room) {
    const Result& result = room.result;
    auto w = client->write();
    w.u8(ToClient::BotsResult).u8(result.winner < 0 ? 0xff : result.winner).u8(room.reason).vu(result.ticks);
    for (size_t i = 0; i < room.seats.size(); ++i) {
        w.vu(result.kills[i]).vu((int32_t)std::lround(std::max(0.0f, result.dealt[i]) * 100));
    }
    w.send();
}

void Rooms::sendCode(Client* client, const std::string& code) {
    client->write().u8(ClientBound::PartyCode).bytes((const uint8_t*)code.data(), code.size()).send();
}

}
