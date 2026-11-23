// The rooms against a made-up brain and fake clients, without sockets. CMakeLists builds it with the
// sanitizers; it finds the server's data/ itself.

#include "Rooms.h"

#include <cstdio>
#include <memory>
#include <string>
#include <unistd.h>
#include <vector>

#include "Client.h"
#include "Coder/Reader.h"
#include "Coder/Writer.h"
#include "Config.h"
#include "Const/Enums.h"
#include "Const/TankDefinitions.h"
#include "Entity/Misc/TeamEntity.h"
#include "Entity/Tank/TankBody.h"
#include "Game.h"
#include "Misc/BossManager.h"
#include "Misc/ShapeManager.h"
#include "Native/Arena.h"
#include "Native/Camera.h"

namespace {

int failures = 0;

#define CHECK(cond) check((cond), #cond, __LINE__)

void check(bool ok, const char* what, int line) {
    if (ok) return;
    std::printf("FAIL line %d: %s\n", line, what);
    ++failures;
}

using live::Rooms;

constexpr int ROOM = live::ToClient::BotsRoom;
constexpr int LISTS = live::ToClient::BotsLists;
constexpr int RESULT = live::ToClient::BotsResult;

class NoShapes : public ShapeManager {
public:
    explicit NoShapes(ArenaEntity* arena) : ShapeManager(arena) {}

protected:
    int wantedShapes() const override { return 0; }
};

class PlainArena : public ArenaEntity {
public:
    explicit PlainArena(GameServer* game) : ArenaEntity(game) {
        shapes = std::make_unique<NoShapes>(this);
        bossManager.reset();
        state = ArenaState::OPEN;
    }
};

bool living(Entity* e) {
    return TankBody::isTank(e) && !((TankBody*)e)->deletionAnimation && ((TankBody*)e)->healthData.health > 0;
}

class FakeBots;

// Spawns a tank per seat in a plain game, the Player seats' with their moved clients' cameras, as
// Duel does. Team 1's tanks die at `length` ticks.
class FakeMatch : public live::Match {
public:
    FakeMatch(FakeBots& owner, const live::Spec& spec, int length);
    ~FakeMatch() override;

    GameServer* game() override { return world; }
    bool tick() override;
    void coast() override;
    bool alive(int seat) const override { return living(tanks[seat]); }
    int ticksLeft() const override { return -1; }
    live::Result result() const override;

    TankBody* tank(int seat) const { return tanks[seat]; }
    void kill(int seat) { if (living(tanks[seat])) tanks[seat]->destroy(); }

    int ticks = 0, coasts = 0;

private:
    FakeBots& owner;
    live::Spec spec;
    int length;
    GameServer* world;
    std::vector<Ref<TankBody>> tanks;

    bool gone(int team) const;
};

class FakeBots : public live::Bots {
public:
    live::Lists list;
    bool up = true;
    int polls = 0, flushes = 0;
    int length = 100;
    int matches = 0, leftovers = 0;
    std::vector<live::Spec> started;
    FakeMatch* last = nullptr;

    FakeBots() {
        auto tank = [](int id) { return std::array<int, 9>{id, 0, 2, 3, 7, 7, 7, 7, 0}; };
        list.bots.push_back({"Alpha", false, {tank(Tank::Factory), tank(Tank::Fighter)}});
        list.bots.push_back({"Beta", true, {tank(Tank::Sniper)}});
        list.playerTanks = {tank(Tank::Fighter), tank(Tank::Factory), tank(Tank::Sniper), tank(Tank::Basic)};
        list.version = 1;
    }

    void poll() override { ++polls; }
    void flush() override { ++flushes; }
    bool online() const override { return up; }
    const live::Lists& lists() const override { return list; }

    std::unique_ptr<live::Match> start(const live::Spec& spec) override {
        if (!up) return nullptr;
        started.push_back(spec);
        auto match = std::make_unique<FakeMatch>(*this, spec, length);
        last = match.get();
        return match;
    }
};

FakeMatch::FakeMatch(FakeBots& owner, const live::Spec& spec, int length)
    : owner(owner), spec(spec), length(length), tanks(spec.seats.size()) {
    world = new GameServer([](GameServer* g) { return (ArenaEntity*)new PlainArena(g); }, "bots", "Bots");
    TeamEntity* teams[2] = {new TeamEntity(world, Color::TeamBlue), new TeamEntity(world, Color::TeamRed)};
    for (size_t i = 0; i < spec.seats.size(); ++i) {
        Client* client = spec.seats[i].client;
        if (client) client->moveTo(world);
        CameraEntity* camera = client ? (CameraEntity*)client->camera.get() : new CameraEntity(world);
        std::shared_ptr<Inputs> inputs = client ? std::shared_ptr<Inputs>(client->inputs) : std::make_shared<Inputs>();
        TankBody* tank = new TankBody(world, camera, inputs);
        camera->cameraData.setPlayer(tank);
        if (client) {
            camera->relationsData.setParent(tank);
            camera->relationsData.setOwner(tank);
            camera->entityState = EntityStateFlags::needsCreate | EntityStateFlags::needsDelete;
            client->camera->spectatee = nullptr;
        }
        TeamEntity::setTeam(teams[(int)i < spec.n0 ? 0 : 1], tank);
        tank->positionData.setX(400.0 * (double)i);
        tank->healthData.setHealth(tank->healthData.maxHealth);
        tanks[i] = tank;
    }
    ++owner.matches;
}

FakeMatch::~FakeMatch() {
    tanks.clear();
    world->flushDeadClients();
    if (world->clients.size()) ++owner.leftovers;
    delete world;
    --owner.matches;
    if (owner.last == this) owner.last = nullptr;
}

bool FakeMatch::gone(int team) const {
    for (size_t i = 0; i < tanks.size(); ++i) {
        if (((int)i < spec.n0 ? 0 : 1) == team && living(tanks[i])) return false;
    }
    return true;
}

bool FakeMatch::tick() {
    world->tickLoop();
    if (++ticks == length) {
        for (int i = spec.n0; i < (int)tanks.size(); ++i) kill(i);
    }
    return gone(0) || gone(1);
}

void FakeMatch::coast() {
    world->tickLoop();
    ++coasts;
}

live::Result FakeMatch::result() const {
    live::Result r;
    r.ticks = ticks;
    if (gone(0) != gone(1)) r.winner = gone(1) ? 0 : 1;
    for (size_t i = 0; i < tanks.size(); ++i) {
        r.kills[i] = (int)i + 1;
        r.dealt[i] = 10.5f * (float)(i + 1);
    }
    return r;
}

std::vector<uint8_t> bytesOf(const Writer& w) { return std::vector<uint8_t>(w.data(), w.data() + w.size()); }

struct SeatView {
    int who = 0, tank = 0, state = 0;
    std::string name;
};

struct RoomView {
    int phase = -1, flags = 0, n0 = 0, n1 = 0, you = 0, ticks = 0;
    std::vector<SeatView> seats;
};

struct Fake {
    Client* client = nullptr;
    bool closed = false;
    std::vector<std::vector<uint8_t>> packets;

    int count(int header) const {
        int n = 0;
        for (auto& p : packets) n += !p.empty() && p[0] == header;
        return n;
    }

    const std::vector<uint8_t>* last(int header) const {
        for (auto it = packets.rbegin(); it != packets.rend(); ++it) {
            if (!it->empty() && (*it)[0] == header) return &*it;
        }
        return nullptr;
    }

    RoomView room() const {
        RoomView v;
        const std::vector<uint8_t>* p = last(ROOM);
        if (!p) return v;
        Reader r(p->data(), p->size());
        r.at = 1;
        v.phase = r.u8();
        v.flags = r.u8();
        v.n0 = r.u8();
        v.n1 = r.u8();
        v.you = r.u8();
        for (int i = 0; i < v.n0 + v.n1; ++i) {
            SeatView s;
            s.who = r.vu();
            s.tank = r.vu();
            s.state = r.u8();
            s.name = r.stringNT();
            v.seats.push_back(s);
        }
        v.ticks = r.vu();
        check(r.at == p->size(), "BotsRoom fully read", __LINE__);
        return v;
    }

    std::string code() const {
        const std::vector<uint8_t>* p = last(ClientBound::PartyCode);
        return p ? std::string(p->begin() + 1, p->end()) : std::string("?");
    }
};

class Harness {
public:
    FakeBots bots;
    std::unique_ptr<Rooms> rooms;
    GameServer* lobby;
    std::vector<std::unique_ptr<Fake>> fakes;

    Harness() {
        rooms = std::make_unique<Rooms>(bots);
        lobby = new GameServer(rooms->lobbyArena(), "bots", "Bots");
        rooms->open(lobby);
    }

    ~Harness() {
        for (auto& f : fakes) hangUp(*f);
        tick(2);
        rooms.reset();
        lobby->flushDeadClients();
        check(lobby->clients.size() == 0, "lobby emptied", __LINE__);
        check(bots.matches == 0, "every match freed", __LINE__);
        check(bots.leftovers == 0, "no client left in a freed match", __LINE__);
        delete lobby;
    }

    // What the host does as the game timer fires.
    void tick(int n = 1) {
        for (int i = 0; i < n; ++i) {
            if (lobby->clients.size()) lobby->tickLoop();
            else lobby->flushDeadClients();
            rooms->tick();
        }
    }

    Fake& connect(const std::string& party = "") {
        fakes.push_back(std::make_unique<Fake>());
        Fake* f = fakes.back().get();
        f->client = new Client(lobby);
        f->client->sendRaw = [f](const uint8_t* d, size_t n) { f->packets.emplace_back(d, d + n); };
        f->client->closeWs = [this, f]() { hangUp(*f); };
        std::string hex;
        for (unsigned char b : party) {
            hex += "0123456789abcdef"[b & 15];
            hex += "0123456789abcdef"[b >> 4];
        }
        Writer w;
        w.u8(ServerBound::Init).stringNT(config::buildHash).stringNT("").stringNT(hex).u8(0);
        send(*f, w);
        tick();
        return *f;
    }

    // What the host does when a socket closes.
    void hangUp(Fake& f) {
        if (f.closed) return;
        f.closed = true;
        Client* c = f.client;
        rooms->onClose(c);
        c->onClose();
        c->game->deadClients.push_back(c);
    }

    void send(Fake& f, const std::vector<uint8_t>& data) {
        if (!f.closed) f.client->onMessage(data.data(), data.size(), true);
    }
    void send(Fake& f, const Writer& w) { send(f, bytesOf(w)); }
    void send(Fake& f, int header) { send(f, std::vector<uint8_t>{(uint8_t)header}); }

    static void build(Writer& w, const std::optional<std::array<int, 8>>& b) {
        w.u8(b ? 1 : 0);
        if (b) for (int x : *b) w.u8(x);
    }

    void create(Fake& f, int flags, int n0, int n1, const std::vector<std::pair<int, int>>& seats,
                const std::string& name, const std::optional<std::array<int, 8>>& b = std::nullopt) {
        Writer w;
        w.u8(live::ToServer::BotsCreate).u8(flags).u8(n0).u8(n1);
        for (auto [who, tank] : seats) w.vu(who).vu(tank);
        w.stringNT(name);
        build(w, b);
        send(f, w);
    }

    void ready(Fake& f, const std::string& name, const std::optional<std::array<int, 8>>& b = std::nullopt) {
        Writer w;
        w.u8(live::ToServer::BotsReady).stringNT(name);
        build(w, b);
        send(f, w);
    }

    // Cut off mid-tick: a malformed packet kicks the client inside a tickLoop.
    void kick(Fake& f) { send(f, std::vector<uint8_t>{(uint8_t)live::ToServer::BotsReady, 'x'}); }

    void drop(Fake& f, int how) {
        if (how == 0) send(f, live::ToServer::BotsLeave);
        else if (how == 1) hangUp(f);
        else kick(f);
    }
};

const char* HOW[3] = {"leave", "close", "kick"};

void testGolden() {
    Harness h;
    Fake& a = h.connect();
    CHECK(a.count(ClientBound::Accept) == 1);
    CHECK(a.count(LISTS) == 1);
    h.send(a, std::vector<uint8_t>{0x20, 0x00, 0x01, 0x01, 0x00, 0x18, 0x01, 0x34, 0x6d, 0x65, 0x00, 0x00});
    h.tick();
    CHECK(h.rooms->size() == 1);
    RoomView v = a.room();
    CHECK(v.phase == Rooms::Countdown);
    CHECK(v.flags == 0 && v.n0 == 1 && v.n1 == 1 && v.you == 0);
    CHECK(v.ticks == Rooms::COUNTDOWN);
    CHECK(v.seats.size() == 2);
    if (v.seats.size() == 2) {
        CHECK(v.seats[0].who == 0 && v.seats[0].tank == Tank::Fighter && v.seats[0].state == 3 && v.seats[0].name == "me");
        CHECK(v.seats[1].who == 1 && v.seats[1].tank == Tank::Factory && v.seats[1].state == 3 &&
              v.seats[1].name == "Alpha");
    }
    h.tick(Rooms::COUNTDOWN);
    CHECK(h.bots.started.size() == 1);
    if (h.bots.started.size() == 1) {
        const live::Spec& s = h.bots.started[0];
        CHECK(s.n0 == 1 && s.n1 == 1 && !s.deterministic && s.seats.size() == 2);
        CHECK(s.seats[0].bot == -1 && s.seats[0].tank == Tank::Fighter && s.seats[0].name == "me" && !s.seats[0].build);
        CHECK(s.seats[1].bot == 0 && s.seats[1].tank == Tank::Factory && !s.seats[1].client);
    }
}

void testDuel() {
    Harness h;
    Fake& a = h.connect();
    h.create(a, 0, 1, 1, {{0, Tank::Fighter}, {1, Tank::Factory}}, "  solo player with a long name  ");
    h.tick();
    CHECK(a.room().phase == Rooms::Countdown);
    CHECK(a.code().size() == 4);
    CHECK(a.room().seats[0].name == "solo player with");
    h.tick(Rooms::COUNTDOWN - 1);
    CHECK(h.bots.started.empty());
    CHECK(a.room().phase == Rooms::Countdown);
    int accepts = a.count(ClientBound::Accept);
    h.tick();
    CHECK(h.bots.started.size() == 1 && h.bots.last);
    if (!h.bots.last) return;
    CHECK(a.client->game == h.bots.last->game());
    CHECK(a.count(ClientBound::Accept) == accepts + 1);
    CHECK(a.room().phase == Rooms::Playing);

    int rooms = a.count(ROOM);
    h.tick(50);
    CHECK(a.client->camera->cameraData.player.get() == h.bots.last->tank(0));
    CHECK(a.count(ROOM) - rooms == 2);
    CHECK(a.room().ticks == 50);
    CHECK(a.count(RESULT) == 0);
    h.tick(49);
    CHECK(a.count(RESULT) == 0);
    h.tick();
    CHECK(a.count(RESULT) == 1);
    RoomView v = a.room();
    CHECK(v.phase == Rooms::Over);
    CHECK(v.ticks == 100);
    CHECK(v.seats[0].state == 7 && v.seats[1].state == 3);
    const std::vector<uint8_t>* p = a.last(RESULT);
    std::vector<uint8_t> expected{RESULT, 0, Rooms::Eliminated, 100, 1, 0x9a, 0x08, 2, 0xb4, 0x10};
    CHECK(p && *p == expected);

    h.tick(10);
    CHECK(h.bots.last && h.bots.last->coasts == 10);
    h.send(a, live::ToServer::BotsLeave);
    h.tick(2);
    CHECK(a.client->game == h.lobby);
    CHECK(a.room().phase == Rooms::None);
    CHECK(a.room().you == 0xff);
    CHECK(h.rooms->size() == 0);
    CHECK(h.bots.matches == 0);
    int lists = a.count(LISTS);
    CHECK(lists == 2);
    CHECK(h.bots.polls == h.bots.flushes);
}

void testTeams() {
    Harness h;
    Fake& a = h.connect();
    h.create(a, 1, 2, 2, {{0, Tank::Fighter}, {0, Tank::Sniper}, {1, Tank::Factory}, {2, Tank::Sniper}}, "ay");
    h.tick();
    RoomView v = a.room();
    CHECK(v.phase == Rooms::Waiting && v.flags == 1 && v.n0 == 2 && v.n1 == 2 && v.you == 0);
    CHECK(v.seats.size() == 4 && v.seats[1].state == 0 && v.seats[3].name == "Beta");
    std::string code = a.code();
    CHECK(code.size() == 4);

    Fake& b = h.connect(code);
    CHECK(b.count(ClientBound::InvalidParty) == 0);
    h.tick();
    CHECK(b.code() == code);
    v = b.room();
    CHECK(v.phase == Rooms::Waiting && v.you == 1 && v.seats[1].state == 1);
    CHECK(a.room().seats[1].state == 1);
    // Accept, its empty code, the lists, then the room's code and the room.
    std::vector<int> order;
    for (auto& packet : b.packets) {
        int header = packet[0];
        if (header == ClientBound::Accept || header == ClientBound::PartyCode || header == LISTS || header == ROOM)
            if (order.empty() || order.back() != header) order.push_back(header);
    }
    CHECK((order == std::vector<int>{ClientBound::Accept, ClientBound::PartyCode, LISTS, ClientBound::PartyCode, ROOM}));

    Fake& c = h.connect(code);
    CHECK(c.count(ClientBound::InvalidParty) == 1);
    h.tick(4);
    CHECK(c.closed);

    h.ready(b, "bee");
    h.tick();
    CHECK(a.room().phase == Rooms::Countdown && a.room().seats[1].state == 3 && a.room().seats[1].name == "bee");
    h.tick(Rooms::COUNTDOWN);
    CHECK(h.bots.started.size() == 1);
    if (h.bots.started.size() != 1) return;
    const live::Spec& s = h.bots.started[0];
    CHECK(s.n0 == 2 && s.n1 == 2 && s.deterministic);
    CHECK(s.seats[0].client == a.client && s.seats[1].client == b.client && s.seats[1].name == "bee");
    CHECK(s.seats[2].bot == 0 && s.seats[3].bot == 1 && s.seats[3].tank == Tank::Sniper);
    CHECK(a.room().phase == Rooms::Playing && b.room().phase == Rooms::Playing);

    // Rematch: both vote and go through the countdown again.
    h.tick(100);
    CHECK(a.room().phase == Rooms::Over && b.count(RESULT) == 1);
    h.send(a, live::ToServer::BotsRematch);
    h.tick();
    CHECK(b.room().seats[0].state & 8);
    CHECK(b.room().phase == Rooms::Over);
    h.send(b, live::ToServer::BotsRematch);
    h.tick();
    CHECK(a.room().phase == Rooms::Countdown && b.room().phase == Rooms::Countdown);
    CHECK(a.client->game == h.lobby && b.client->game == h.lobby);
    CHECK(h.bots.matches == 0);
    CHECK(a.code() == code);
    h.tick(Rooms::COUNTDOWN);
    CHECK(h.bots.started.size() == 2 && a.room().phase == Rooms::Playing);

    // A rematch with an empty seat waits for a guest.
    h.tick(100);
    CHECK(a.count(RESULT) == 2);
    h.send(b, live::ToServer::BotsLeave);
    h.send(a, live::ToServer::BotsRematch);
    h.tick(2);
    CHECK(b.room().phase == Rooms::None && b.client->game == h.lobby);
    v = a.room();
    CHECK(v.phase == Rooms::Waiting && v.seats[1].state == 0 && v.seats[1].name.empty());
    CHECK(a.code() == code);
    Fake& d = h.connect(code);
    h.ready(d, "dee");
    h.tick(2);
    CHECK(a.room().phase == Rooms::Countdown && d.room().you == 1);
}

void testWrongCode() {
    Harness h;
    Fake& a = h.connect(std::string("\x01\x02\x03\x04", 4));
    CHECK(a.count(ClientBound::InvalidParty) == 1);
    CHECK(a.count(ClientBound::Accept) == 0);
    h.tick(4);
    CHECK(a.closed);

    // Only Waiting rooms with a free Player seat take guests.
    Fake& b = h.connect();
    h.create(b, 0, 1, 1, {{0, Tank::Fighter}, {1, Tank::Factory}}, "b");
    h.tick();
    std::string code = b.code();
    Fake& c = h.connect(code);
    CHECK(c.count(ClientBound::InvalidParty) == 1);
    h.tick(Rooms::COUNTDOWN + 1);
    CHECK(b.room().phase == Rooms::Playing);
    // In a match the link is just the server's.
    CHECK(b.code().empty());
    Fake& d = h.connect(code);
    CHECK(d.count(ClientBound::InvalidParty) == 1);
}

void testBadCreate() {
    Harness h;
    Fake& a = h.connect();
    auto refused = [&](int flags, int n0, int n1, const std::vector<std::pair<int, int>>& seats) {
        int notes = a.count(ClientBound::Notification);
        h.create(a, flags, n0, n1, seats, "a");
        h.tick();
        return h.rooms->size() == 0 && a.count(ClientBound::Notification) == notes + 1 && a.count(ROOM) == 0;
    };
    CHECK(refused(0, 1, 1, {{0, Tank::Fighter}, {3, Tank::Factory}}));
    CHECK(refused(0, 1, 1, {{0, Tank::Fighter}, {1, Tank::Sniper}}));
    CHECK(refused(0, 1, 1, {{0, Tank::Twin}, {1, Tank::Factory}}));
    CHECK(refused(0, 1, 1, {{0, 100000}, {1, Tank::Factory}}));
    CHECK(refused(0, 1, 1, {{1, Tank::Factory}, {2, Tank::Sniper}}));
    CHECK(refused(0, 3, 1, {{0, Tank::Fighter}, {1, Tank::Factory}, {1, Tank::Factory}, {1, Tank::Factory}}));
    CHECK(refused(0, 0, 1, {{0, Tank::Fighter}}));
    h.bots.up = false;
    CHECK(refused(0, 1, 1, {{0, Tank::Fighter}, {1, Tank::Factory}}));
    h.bots.up = true;

    // A cut-short packet kicks, as any malformed packet does.
    h.send(a, std::vector<uint8_t>{0x20, 0x00, 0x01, 0x01, 0x00});
    h.tick(2);
    CHECK(a.closed && h.rooms->size() == 0);

    // At most MAX_ROOMS rooms.
    std::vector<Fake*> owners;
    for (int i = 0; i <= Rooms::MAX_ROOMS; ++i) {
        Fake& f = h.connect();
        h.create(f, 0, 1, 1, {{0, Tank::Fighter}, {1, Tank::Factory}}, "f");
        owners.push_back(&f);
    }
    h.tick();
    CHECK((int)h.rooms->size() == Rooms::MAX_ROOMS);
    CHECK(owners.back()->count(ClientBound::Notification) == 1 && owners.back()->count(ROOM) == 0);
}

// A player's own build goes to the match if the tank allows it: within its caps, 33 points at most.
void testBuilds() {
    using Build = std::array<int, 8>;
    Harness h;
    Fake& a = h.connect();
    auto refused = [&](int tank, const Build& b) {
        int notes = a.count(ClientBound::Notification);
        h.create(a, 0, 1, 1, {{0, tank}, {1, Tank::Factory}}, "a", b);
        h.tick();
        return h.rooms->size() == 0 && a.count(ClientBound::Notification) == notes + 1;
    };
    CHECK(refused(Tank::Fighter, {8, 7, 7, 7, 4, 0, 0, 0}));
    CHECK(refused(Tank::Fighter, {7, 7, 7, 7, 6, 0, 0, 0}));
    CHECK(refused(Tank::Basic, {7, 7, 7, 7, 0, 0, 0, 0xff}));

    Build mine{7, 7, 7, 7, 5, 0, 0, 0}, theirs{0, 0, 0, 3, 7, 7, 7, 2}, later{1, 1, 1, 1, 1, 1, 1, 1};
    h.create(a, 0, 2, 1, {{0, Tank::Fighter}, {0, Tank::Sniper}, {1, Tank::Factory}}, "a", mine);
    h.tick();
    CHECK(a.room().phase == Rooms::Waiting);
    Fake& b = h.connect(a.code());
    int notes = b.count(ClientBound::Notification);
    h.ready(b, "b", Build{0, 0, 0, 0, 0, 0, 0, 11});
    h.tick();
    CHECK(b.count(ClientBound::Notification) == notes + 1 && a.room().seats[1].state == 1);
    h.ready(a, "a", later);
    h.ready(b, "b", theirs);
    h.tick(Rooms::COUNTDOWN + 1);
    CHECK(h.bots.started.size() == 1);
    if (h.bots.started.size() != 1) return;
    const live::Spec& s = h.bots.started[0];
    CHECK(s.seats[0].build == later && s.seats[1].build == theirs && !s.seats[2].build);
}

// Two humans and a bot; the second human drops out in `phase`, then the first.
void testDrop(int phase, int how) {
    Harness h;
    Fake& a = h.connect();
    h.create(a, 0, 2, 1, {{0, Tank::Fighter}, {0, Tank::Basic}, {1, Tank::Factory}}, "a");
    h.tick();
    int before = failures;
    Fake& b = h.connect(a.code());
    if (phase >= Rooms::Countdown) h.ready(b, "b");
    h.tick();
    if (phase >= Rooms::Playing) h.tick(Rooms::COUNTDOWN);
    if (phase >= Rooms::Over) h.tick(100);
    char label[64];
    std::snprintf(label, sizeof(label), "phase %d, %s", phase, HOW[how]);
    if (a.room().phase != phase) {
        std::printf("FAIL %s: reached phase %d\n", label, a.room().phase);
        ++failures;
        return;
    }

    h.drop(b, how);
    h.tick(2);
    RoomView v = a.room();
    if (phase == Rooms::Countdown) CHECK(v.phase == Rooms::Waiting);
    else CHECK(v.phase == phase);
    CHECK((v.seats[1].state & 3) == 0);
    if (phase == Rooms::Playing) CHECK((v.seats[1].state & 4) == 0 && (v.seats[0].state & 4));
    if (how == 0) {
        CHECK(b.client->game == h.lobby && Entity::exists(b.client->camera));
        CHECK(b.room().phase == Rooms::None);
        if (phase <= Rooms::Countdown) CHECK(b.code().empty());
    } else {
        CHECK(b.closed);
    }
    if (phase == Rooms::Playing) {
        h.tick(100);
        CHECK(a.room().phase == Rooms::Over);
        CHECK(a.last(RESULT) && (*a.last(RESULT))[1] == 0);
    }

    h.drop(a, how);
    h.tick(2);
    CHECK(h.rooms->size() == 0);
    CHECK(h.bots.matches == 0);
    if (how == 0) CHECK(a.client->game == h.lobby && a.room().phase == Rooms::None);
    if (failures != before) std::printf("  in %s\n", label);
}

// Gone before the tick that would have moved it back to the lobby.
void testLeaveThenClose(int phase) {
    Harness h;
    Fake& a = h.connect();
    h.create(a, 0, 2, 1, {{0, Tank::Fighter}, {0, Tank::Basic}, {1, Tank::Factory}}, "a");
    h.tick();
    Fake& b = h.connect(a.code());
    h.ready(b, "b");
    h.tick(Rooms::COUNTDOWN + 1);
    if (phase == Rooms::Over) h.tick(100);
    CHECK(a.room().phase == phase);
    h.send(b, live::ToServer::BotsLeave);
    h.tick();
    h.hangUp(b);
    h.tick(2);
    CHECK((a.room().seats[1].state & 3) == 0);
    h.send(a, live::ToServer::BotsLeave);
    h.hangUp(a);
    h.tick(2);
    CHECK(h.rooms->size() == 0 && h.bots.matches == 0);
}

// A dead human watches a living teammate, then anyone left.
void testSpectate() {
    Harness h;
    Fake& a = h.connect();
    h.create(a, 0, 2, 2, {{0, Tank::Basic}, {0, Tank::Basic}, {1, Tank::Factory}, {2, Tank::Sniper}}, "a");
    h.tick();
    Fake& b = h.connect(a.code());
    h.ready(b, "b");
    h.tick(Rooms::COUNTDOWN + 2);
    FakeMatch* match = h.bots.last;
    CHECK(match && a.room().phase == Rooms::Playing);
    if (!match) return;
    match->kill(0);
    h.tick(2);
    CHECK(a.room().phase == Rooms::Playing && (a.room().seats[0].state & 4) == 0);
    CHECK(a.client->camera->spectatee.get() == match->tank(1));
    // Nobody respawns or takes a tank in a match.
    h.tick(10);
    CHECK(!Entity::exists(a.client->camera->cameraData.player));
    int notes = b.count(ClientBound::Notification);
    Writer spawn;
    spawn.u8(ServerBound::Spawn).stringNT("again");
    h.send(a, spawn);
    h.send(b, ServerBound::TakeTank);
    h.tick(3);
    CHECK(!Entity::exists(a.client->camera->cameraData.player));
    CHECK(b.count(ClientBound::Notification) == notes);
    match->kill(1);
    h.tick(2);
    CHECK(a.room().phase == Rooms::Over && a.last(RESULT) && (*a.last(RESULT))[1] == 1);
    TankBody* watched = (TankBody*)a.client->camera->spectatee.get();
    CHECK(watched == match->tank(2) || watched == match->tank(3));
    watched = (TankBody*)b.client->camera->spectatee.get();
    CHECK(watched == match->tank(2) || watched == match->tank(3));
    h.tick(20);
    CHECK(living(a.client->camera->spectatee));
}

// A 1v1 between two humans: whoever leaves mid-match loses by leaving.
void testLeftResult() {
    Harness h;
    Fake& a = h.connect();
    h.create(a, 0, 1, 1, {{0, Tank::Fighter}, {0, Tank::Sniper}}, "a");
    h.tick();
    Fake& b = h.connect(a.code());
    h.ready(b, "b");
    h.tick(Rooms::COUNTDOWN + 2);
    CHECK(a.room().phase == Rooms::Playing);
    h.send(b, live::ToServer::BotsLeave);
    h.tick(3);
    CHECK(a.room().phase == Rooms::Over);
    const std::vector<uint8_t>* p = a.last(RESULT);
    CHECK(p && (*p)[1] == 0 && (*p)[2] == Rooms::Left);
    CHECK(b.count(RESULT) == 0 && b.room().phase == Rooms::None);
}

void testOffline() {
    Harness h;
    h.bots.length = 100000;
    Fake& a = h.connect();
    h.create(a, 0, 1, 1, {{0, Tank::Fighter}, {1, Tank::Factory}}, "a");
    h.tick(Rooms::COUNTDOWN + 1);
    CHECK(a.room().phase == Rooms::Playing);
    int notes = a.count(ClientBound::Notification);
    h.bots.up = false;
    h.tick(Rooms::OFFLINE_LIMIT - 1);
    CHECK(a.count(ClientBound::Notification) == notes + 1);
    CHECK(a.room().phase == Rooms::Playing);
    h.tick();
    CHECK(a.room().phase == Rooms::Over);
    const std::vector<uint8_t>* p = a.last(RESULT);
    CHECK(p && (*p)[1] == 0xff && (*p)[2] == Rooms::Offline);

    // The countdown waits for the brain.
    h.send(a, live::ToServer::BotsRematch);
    h.tick(Rooms::COUNTDOWN * 2);
    CHECK(a.room().phase == Rooms::Waiting && h.bots.started.size() == 1);
    h.bots.up = true;
    h.tick(Rooms::COUNTDOWN + 2);
    CHECK(a.room().phase == Rooms::Playing && h.bots.started.size() == 2);
}

void testLists() {
    Harness h;
    Fake& a = h.connect();
    const std::vector<uint8_t>* p = a.last(LISTS);
    std::vector<uint8_t> expected{LISTS, 1, 2, 'A', 'l', 'p', 'h', 'a', 0, 2, Tank::Factory, Tank::Fighter,
                                  'B', 'e', 't', 'a', 0, 1, Tank::Sniper, 4};
    for (int tank : {Tank::Fighter, Tank::Factory, Tank::Sniper, Tank::Basic})
        expected.insert(expected.end(), {(uint8_t)tank, 0, 2, 3, 7, 7, 7, 7, 0});
    CHECK(p && *p == expected);
    h.tick(5);
    CHECK(a.count(LISTS) == 1);
    h.bots.list.bots.pop_back();
    h.bots.list.version = 2;
    h.tick();
    CHECK(a.count(LISTS) == 2);
    h.bots.up = false;
    h.tick();
    CHECK(a.count(LISTS) == 3 && (*a.last(LISTS))[1] == 0);

    // A room whose bot vanished from the lists waits, and says so once.
    h.bots.up = true;
    h.create(a, 0, 1, 1, {{0, Tank::Fighter}, {1, Tank::Factory}}, "a");
    h.tick();
    int notes = a.count(ClientBound::Notification);
    h.bots.list.bots[0].name = "Gamma";
    h.tick(Rooms::COUNTDOWN + 5);
    CHECK(a.room().phase == Rooms::Waiting && h.bots.started.empty());
    CHECK(a.count(ClientBound::Notification) == notes + 1);
}

}

int main() {
    if (chdir(SERVER_DIR) != 0) {
        std::printf("can't enter the server folder\n");
        return 1;
    }
    config::init();
    initTankDefinitions();

    testGolden();
    testDuel();
    testTeams();
    testWrongCode();
    testBadCreate();
    testBuilds();
    for (int phase = Rooms::Waiting; phase <= Rooms::Over; ++phase) {
        for (int how = 0; how < 3; ++how) testDrop(phase, how);
    }
    testLeaveThenClose(Rooms::Playing);
    testLeaveThenClose(Rooms::Over);
    testSpectate();
    testLeftResult();
    testOffline();
    testLists();

    for (TankDefinition* definition : TankDefinitions) delete definition;
    TankDefinitions.clear();
    if (failures) {
        std::printf("%d failures\n", failures);
        return 1;
    }
    std::printf("rooms tests OK\n");
    return 0;
}
