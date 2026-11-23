// Live matches without the server, as fast as the brain answers.
//
//   exam [--socket path] [--format 1v1|2v1|1v2|2v2] [--a Bot] [--b Bot] [--games 100] [--deterministic]
//        [--realtime]
//   exam --fake
//
// The first plays bot against bot through the brain: bot a fills team 0's seats and bot b team 1's, each seat dealt
// one of its bot's classes at random, and a game still going after the training time limit is a draw. --realtime
// paces it at the server's 25 ticks a second and counts answers that came on time, late or never.
// --fake needs no brain: a client seated as a Human, standing still, against a scripted fighter.
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>

#include "Brain.h"
#include "Client.h"
#include "Config.h"
#include "Const/Enums.h"
#include "Const/TankDefinitions.h"
#include "Duel.h"
#include "Entity/Tank/Projectile/Drone.h"
#include "Entity/Tank/TankBody.h"
#include "Game.h"
#include "Native/Arena.h"
#include "Native/Manager.h"
#include "Util.h"

using namespace live;

namespace {

int botIndex(const Lists& lists, const std::string& name) {
    for (size_t i = 0; i < lists.bots.size(); ++i)
        if (lists.bots[i].name == name) return (int)i;
    throw std::runtime_error("the brain has no bot called " + name);
}

std::unique_ptr<Client> quietClient(GameServer* game) {
    auto c = std::make_unique<Client>(game);
    c->sendRaw = [](const uint8_t*, size_t) {};
    c->closeWs = [] {};
    c->acceptClient();
    return c;
}

int fake() {
    GameServer* lobby = new GameServer([](GameServer* g) { return new ArenaEntity(g); }, "lobby", "Lobby");
    auto client = quietClient(lobby);
    bots::Config c;
    c.kinds[0] = bots::Human;
    c.kinds[1] = bots::Fighter;
    c.tanks[0] = c.tanks[1] = 24;
    c.endless = true;
    c.spawnMin = 600;
    c.spawnMax = 800;
    bots::Duel duel(c, bots::Duel::Deferred{});
    duel.seat(0, client.get());
    duel.names[0] = "me";
    duel.reset();
    if (client->game != duel.game) throw std::runtime_error("the client wasn't moved into the match");
    TankBody* mine = duel.tank(0);
    if (!mine || mine->cameraEntity.get() != (CameraEntity*)client->camera.get())
        throw std::runtime_error("the client doesn't own slot 0's tank");
    double x0 = mine->positionData.x;
    client->inputs->flags = x0 > 0 ? InputFlags::left : InputFlags::right;
    bots::Action none[bots::MAX_PLAYERS];
    bots::Outcome out[bots::MAX_PLAYERS];
    std::vector<bots::Credit> credits;
    int decisions = 0;
    bool ended = false, moved = false, listed = false;
    while (!ended && decisions < 2000) {
        duel.begin(none);
        for (int r = 0; r < c.repeat; ++r)
            if (duel.advance()) break;
        ended = duel.settle(out, credits, false);
        decisions += 1;
        moved |= duel.isAlive(0) && std::abs(duel.tank(0)->positionData.x - x0) > 50;
        listed |= duel.game->arena->arenaData.scoreboardAmount == 2;
    }
    if (!moved) throw std::runtime_error("the seated client's inputs didn't move its tank");
    if (!listed) throw std::runtime_error("the scoreboard doesn't list both tanks");
    if (!ended || duel.winningTeam() < 0 || duel.isAlive(0)) throw std::runtime_error("the fighter didn't win");
    // Spawning again is refused.
    uint8_t spawn[] = {2, 'm', 'e', 0};
    client->onMessage(spawn, sizeof(spawn), true);
    for (int i = 0; i < 100; ++i) duel.coast();
    if (duel.isAlive(0)) throw std::runtime_error("the client respawned");
    std::printf("fake: moved, died after %d decisions, stayed dead\n", decisions);

    // A drone tank in a 1v1 starts with its drones on its own side.
    bots::Config d;
    d.kinds[0] = bots::Human;
    d.kinds[1] = bots::Still;
    d.tanks[0] = 12;
    d.tanks[1] = 52;
    d.endless = true;
    d.arenaSize = 6000;
    d.spawnMin = d.spawnMax = 3000;
    bots::Duel drones(d, bots::Duel::Deferred{});
    drones.seat(0, client.get());
    drones.reset();
    for (int i = 0; i < 10; ++i) {
        drones.begin(none);
        for (int r = 0; r < d.repeat; ++r) drones.advance();
        drones.settle(out, credits, false);
    }
    TankBody* overlord = drones.tank(0);
    if (!overlord) throw std::runtime_error("the Overlord died within 10 decisions");
    int own = 0, strays = 0;
    EntityManager* em = drones.game->entities.get();
    for (int id = 0; id <= em->lastId; ++id) {
        Entity* e = em->inner[id];
        if (!Entity::exists(e) || !dynamic_cast<Drone*>(e)) continue;
        ObjectEntity* o = (ObjectEntity*)e;
        if (o->relationsData.owner.get() != (Entity*)overlord) continue;
        (o->relationsData.team.get() == overlord->relationsData.team.get() ? own : strays) += 1;
    }
    if (own == 0 || strays > 0)
        throw std::runtime_error("the Overlord's drones: " + std::to_string(own) + " on its side, " +
                                 std::to_string(strays) + " not");
    if (overlord->healthData.health < overlord->healthData.maxHealth)
        throw std::runtime_error("the Overlord was hurt standing alone: " + std::to_string(overlord->healthData.health) +
                                 " of " + std::to_string(overlord->healthData.maxHealth));
    std::printf("fake: an Overlord's %d drones stayed on its side\n", own);
    client->moveTo(lobby);
    return 0;
}

}

// Run from server/, so its data files are found.
int main(int argc, char** argv) {
    config::init();
    initTankDefinitions();
    std::string socketPath = "../bots/runs/live.sock", format = "2v2", a = "Bot", b = "Bot";
    int games = 100;
    bool deterministic = false, realtime = false;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        auto next = [&]() -> std::string {
            if (i + 1 >= argc) throw std::runtime_error(arg + " needs a value");
            return argv[++i];
        };
        if (arg == "--fake") return fake();
        if (arg == "--socket") socketPath = next();
        else if (arg == "--format") format = next();
        else if (arg == "--a") a = next();
        else if (arg == "--b") b = next();
        else if (arg == "--games") games = std::stoi(next());
        else if (arg == "--deterministic") deterministic = true;
        else if (arg == "--realtime") realtime = true;
        else throw std::runtime_error("unknown argument " + arg);
    }
    if (format.size() != 3 || format[1] != 'v') throw std::runtime_error("format is like 2v1");
    int n0 = format[0] - '0', n1 = format[2] - '0';
    BrainBots brain(socketPath);
    std::printf("waiting for the brain on %s\n", socketPath.c_str());
    if (!brain.waitForBrain(120000)) throw std::runtime_error("no brain");
    const Lists& lists = brain.lists();
    int ia = botIndex(lists, a), ib = botIndex(lists, b);
    int limit = (n0 + n1 > 2 ? 900 : 375) * 4;
    int wins[3] = {}, onTime = 0, late = 0;
    long ticks = 0;
    for (int g = 0; g < games; ++g) {
        Spec spec;
        spec.n0 = n0;
        spec.n1 = n1;
        spec.deterministic = deterministic;
        for (int i = 0; i < n0 + n1; ++i) {
            const Lists::Bot& bot = lists.bots[i < n0 ? ia : ib];
            Seat seat;
            seat.bot = i < n0 ? ia : ib;
            seat.tank = bot.tanks[(size_t)(util::randomFloat() * bot.tanks.size()) % bot.tanks.size()][0];
            seat.name = bot.name;
            spec.seats.push_back(seat);
        }
        std::unique_ptr<Match> match = brain.start(spec);
        if (!match) throw std::runtime_error("the brain refused the match");
        bool over = false;
        auto next = std::chrono::steady_clock::now();
        int t = 0;
        while (!over && t < limit) {
            if (realtime) {
                brain.flush();
                next += std::chrono::milliseconds(40);
                std::this_thread::sleep_until(next);
                brain.poll();
                if (brain.online()) onTime += 1;
                else late += 1;
            } else if (!brain.sync()) {
                throw std::runtime_error("the brain went away");
            }
            over = match->tick();
            t += 1;
        }
        Result r = match->result();
        wins[over && r.winner >= 0 ? r.winner : 2] += 1;
        ticks += t;
    }
    std::printf("%s, %s vs %s, %d games%s: team 0 won %.1f%%, team 1 %.1f%%, draws %.1f%%, %.0f ticks a game\n",
                format.c_str(), a.c_str(), b.c_str(), games, deterministic ? ", best moves" : "",
                100.0 * wins[0] / games, 100.0 * wins[1] / games, 100.0 * wins[2] / games, (double)ticks / games);
    if (realtime) std::printf("ticks with the brain keeping up %d, behind %d\n", onTime, late);
    return 0;
}
