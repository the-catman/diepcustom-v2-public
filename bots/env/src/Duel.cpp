#include "Duel.h"
#include "DuelShapes.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <mutex>
#include <stdexcept>

#include "Client.h"
#include "Const/Enums.h"
#include "Const/TankDefinitions.h"
#include "Entity/Live.h"
#include "Entity/Misc/TeamEntity.h"
#include "Entity/Object.h"
#include "Entity/Shape/AbstractShape.h"
#include "Entity/Shape/Crasher.h"
#include "Entity/Tank/Barrel.h"
#include "Entity/Tank/Projectile/Bullet.h"
#include "Entity/Tank/Projectile/Drone.h"
#include "Entity/Tank/TankBody.h"
#include "Game.h"
#include "Misc/BossManager.h"
#include "Misc/ShapeManager.h"
#include "Native/Arena.h"
#include "Native/Camera.h"
#include "Native/Manager.h"
#include "Physics/CollisionManager.h"
#include "Util.h"

namespace bots {

namespace {

// GameServer's constructor and destructor edit the list of every game.
std::mutex gamesMutex;

constexpr double VEL = 40;
constexpr double POS = 1000;
// Decisions after which a last sighting counts as fully stale.
constexpr int SEEN_CAP = 64;
// Far enough that the tank moving during a tick doesn't swing the aim.
constexpr double MOUSE = 10000;
// The farthest a placed mouse can go.
constexpr double MAX_REACH = 3000;
constexpr int WINDOWS[3] = {4, 8, 16};
// Ticks a recorded game runs on past its end, so the replay shows how it ended.
constexpr int REPLAY_HOLD = 25;

class DuelArena : public ArenaEntity {
public:
    DuelArena(GameServer* game, double size, const ShapeMix& mix) : ArenaEntity(game) {
        shapes = std::make_unique<DuelShapes>(this, mix);
        bossManager.reset();
        updateBounds(size, size);
        state = ArenaState::OPEN;
    }

    DuelShapes* duelShapes() { return (DuelShapes*)shapes.get(); }

    // With someone watching, the scoreboard lists every tank, not only the clients'.
    Duel* duel = nullptr;

    void updateArenaState() override {
        if (!duel) return ArenaEntity::updateArenaState();
        if (((int)(game->tick % (int)config::scoreboardUpdateInterval)) != 0) return;
        std::vector<TankBody*> tanks;
        for (int i = 0; i < duel->config.players; ++i) {
            TankBody* t = duel->tank(i);
            if (t && !t->deletionAnimation) tanks.push_back(t);
        }
        updateScoreboard(tanks);
    }
};

double uniform(double a, double b) { return a + (b - a) * util::randomFloat(); }

double gauss() {
    double u = std::max(util::randomFloat(), 1e-12), v = util::randomFloat();
    return std::sqrt(-2 * std::log(u)) * std::cos(util::PI2 * v);
}

int quantize(double x, double y) {
    if (x * x + y * y < 1e-6) return 0;
    int k = (int)std::lround(std::atan2(y, x) / (M_PI / 4));
    return ((k % 8) + 8) % 8 + 1;
}

float clampf(double v, double lo, double hi) { return (float)std::min(hi, std::max(lo, v)); }

bool alive(TankBody* t) { return t && !t->deletionAnimation && t->healthData.health > 0; }

// Dominators play the whole game at level 1, kills and all.
bool levelOne(int tank) { return tank == Tank::DominatorD || tank == Tank::DominatorG || tank == Tank::DominatorT; }

// World to a tank's frame and back.
struct Frame {
    int rot;
    bool flip;
    explicit Frame(int f) : rot(f & 3), flip(f >= 4) {}
    void vec(double& x, double& y) const {
        if (flip) y = -y;
        for (int i = 0; i < rot; ++i) {
            double t = x;
            x = -y;
            y = t;
        }
    }
    double angle(double a) const { return (flip ? -a : a) + rot * M_PI / 2; }
    double toWorld(double a) const {
        a -= rot * M_PI / 2;
        return flip ? -a : a;
    }
    // A lead is an angle between two directions, so only mirroring changes it.
    double lead(double l) const { return flip ? -l : l; }
};

}

Config::Config() {
    static constexpr int BUILD[8] = {5, 7, 6, 6, 5, 0, 4, 0};
    for (auto& s : stats) std::copy_n(BUILD, 8, s);
    std::fill(std::begin(kinds), std::end(kinds), (int)External);
    kinds[1] = Fighter;
    std::fill(std::begin(learner), std::end(learner), true);
    std::fill(std::begin(teams), std::end(teams), -1);
}

Duel::Duel(const Config& config) : config(config) { reset(); }

Duel::Duel(const Config& config, Deferred) : config(config) {}

void Duel::seat(int slot, Client* client) { clients[slot] = client; }

Duel::~Duel() { destroyGame(); }

TankBody* Duel::tank(int slot) const { return slots[slot].tank.get(); }

std::vector<TankBody*> Duel::tanks() const {
    std::vector<TankBody*> out;
    for (int i = 0; i < config.players; ++i) out.push_back(tank(i));
    return out;
}

bool Duel::isAlive(int slot) const { return slot < config.players && !slots[slot].dead && alive(tank(slot)); }

// A slot on its own counts as a team of one, numbered past every real team.
int Duel::teamOf(int slot) const { return config.teams[slot] >= 0 ? config.teams[slot] : MAX_PLAYERS + slot; }

bool Duel::sameTeam(int a, int b) const { return teamOf(a) == teamOf(b); }

bool Duel::hasTeams() const {
    for (int i = 0; i < config.players; ++i)
        if (config.teams[i] >= 0) return true;
    return false;
}

void Duel::destroyGame() {
    if (!game) return;
    // Refs point into the game's entity table, so drop them first.
    for (Slot& s : slots) {
        s.tank = nullptr;
        s.camera = nullptr;
        s.target = nullptr;
        s.rows.clear();
    }
    for (auto& t : teamEntities) t = nullptr;
    if (recorder) recorder->release();
    // Clients that left during the game are freed at the end of a tick it won't have.
    game->flushDeadClients();
    {
        std::lock_guard<std::mutex> lock(gamesMutex);
        delete game;
        game = nullptr;
    }
    if (recorder) recorder->finish();
}

void Duel::holdForReplay() {
    game->onDamage = nullptr;
    for (int i = 0; i < REPLAY_HOLD; ++i) {
        keepCameras();
        recorder->beforeTick(tanks());
        game->tickLoop();
    }
}

void Duel::keepCameras() {
    // A tank without a client loses its camera when it dies, and ticks on once its death animation
    // ends, which needs one.
    for (TankBody* t : tanks()) {
        if (Entity::exists(t) && !Entity::exists(t->cameraEntity)) t->cameraEntity = new CameraEntity(game);
    }
}

void Duel::startReplay(const std::vector<ReplayView>& views, const std::vector<std::string>& names) {
    if (recorder) throw std::runtime_error("this duel is already recording");
    recorder = std::make_unique<Recorder>();
    recorder->views = views;
    recorder->names = names;
    std::lock_guard<std::mutex> lock(gamesMutex);
    recorder->attach(game, tanks());
}

std::vector<Packet> Duel::takePackets() {
    std::vector<Packet> out;
    if (recorder) out.swap(recorder->packets);
    return out;
}

void Duel::reset() {
    int n = config.players;
    if (n < 2 || n > MAX_PLAYERS) throw std::runtime_error("a game needs 2 to " + std::to_string(MAX_PLAYERS) + " tanks");
    for (int i = 0; i < n; ++i) {
        if (config.teams[i] < -1 || config.teams[i] >= MAX_PLAYERS) throw std::runtime_error("a team is -1 or 0 to 7");
    }
    if (recorder && game) holdForReplay();
    destroyGame();
    double size = config.arenaSize;
    ShapeMix mix;
    if (config.shapesMax > 0) {
        bool none = config.shapesEmpty > 0 && util::randomFloat() < config.shapesEmpty;
        double density = uniform(config.shapesMin, config.shapesMax);
        if (!none) mix.count = (int)std::lround(density * FFA_SHAPE_DENSITY * size * size);
        if (config.pentagonRich > 0 && util::randomFloat() < config.pentagonRich) mix.pentagons = uniform(0.2, 0.4);
    }
    {
        std::lock_guard<std::mutex> lock(gamesMutex);
        auto arena = [size, mix](GameServer* g) { return (ArenaEntity*)new DuelArena(g, size, mix); };
        game = new GameServer(arena, "duel", "Duel");
    }
    game->onDamage = [this](LivingEntity* v, LivingEntity* s, double d) { onDamage(v, s, d); };
    bool seated = false;
    for (int i = 0; i < n; ++i) {
        if (config.kinds[i] != Human) continue;
        if (!clients[i]) throw std::runtime_error("a Human slot needs seat() before reset()");
        clients[i]->moveTo(game);
        seated = true;
    }
    // Nobody spawns again.
    if (seated) {
        game->arena->arenaData.setFlags(game->arena->arenaData.flags | ArenaFlags::noJoining);
        ((DuelArena*)game->arena)->duel = this;
    }
    static constexpr int TEAM_COLORS[4] = {Color::TeamBlue, Color::TeamRed, Color::TeamGreen, Color::TeamPurple};
    for (int i = 0; i < n; ++i) {
        int t = config.teams[i];
        if (t >= 0 && !teamEntities[t]) teamEntities[t] = new TeamEntity(game, TEAM_COLORS[t % 4]);
    }
    decision = 0;
    over = false;
    winner = -1;
    killDecision = -1;
    kills.clear();
    otherDeaths.clear();
    tracks.clear();

    for (int i = 0; i < n; ++i) deal(i);

    double half = size / 2, margin = 300;
    double x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    for (int attempt = 0; attempt < 100 && n == 2; ++attempt) {
        x0 = uniform(-half + margin, half - margin);
        y0 = uniform(-half + margin, half - margin);
        double a = uniform(0, util::PI2), d = uniform(config.spawnMin, config.spawnMax);
        if (config.mirrorSpawn) {
            x0 = std::cos(a) * d / 2;
            y0 = std::sin(a) * d / 2;
        }
        x1 = config.mirrorSpawn ? -x0 : x0 + std::cos(a) * d;
        y1 = config.mirrorSpawn ? -y0 : y0 + std::sin(a) * d;
        if (std::abs(x1) < half - margin && std::abs(y1) < half - margin) break;
    }
    double xs[MAX_PLAYERS] = {x0, x1}, ys[MAX_PLAYERS] = {y0, y1};
    if (hasTeams()) spreadTeams(xs, ys);
    else if (n > 2) spread(xs, ys);
    for (int i = 0; i < n; ++i) spawn(i, xs[i], ys[i]);
    if (config.stockDrones) {
        for (int i = 0; i < n; ++i) stockDrones(i);
    }
    if (mix.count > 0) {
        CollisionManager* grid = game->entities->collisionManager.get();
        grid->preTick(game->tick);
        std::vector<TankBody*> all = tanks();
        ((DuelArena*)game->arena)->duelShapes()->prefill(std::vector<ObjectEntity*>(all.begin(), all.end()), 150);
        grid->postTick(game->tick);
    }

    for (int i = 0; i < n; ++i) {
        Slot& s = slots[i];
        s.action = Action{};
        // Facing the nearest enemy tank, or with none, the nearest other tank.
        int near = -1;
        for (int pass = 0; pass < 2 && near < 0; ++pass) {
            for (int j = 0; j < n; ++j) {
                if (j == i || (pass == 0 && sameTeam(i, j))) continue;
                if (near < 0 || std::hypot(xs[j] - xs[i], ys[j] - ys[i]) < std::hypot(xs[near] - xs[i], ys[near] - ys[i])) {
                    near = j;
                }
            }
        }
        s.heldAngle = std::atan2(ys[near] - ys[i], xs[near] - xs[i]);
        s.heldReach = MOUSE;
        s.strafeDir = util::randomFloat() < 0.5 ? 1 : -1;
        s.nextFlip = (int)uniform(10, 60);
        s.linearAngle = (int)(util::randomFloat() * 8) * M_PI / 4;
        s.noise = 0;
        s.noiseUntil = 0;
        s.frame = config.symmetry ? 2 * (int)(util::randomFloat() * 4) & 6 : 0;
        s.outcome = Outcome{};
        s.owed.clear();
        s.hitBullets.clear();
        s.blockedBullets.clear();
        s.firstSeen = -1;
        std::fill(std::begin(s.sightings), std::end(s.sightings), Sighting{});
        s.dead = false;
        std::fill(std::begin(s.damageFrom), std::end(s.damageFrom), 0.0);
    }
    record();
    if (recorder) {
        std::lock_guard<std::mutex> lock(gamesMutex);
        recorder->attach(game, tanks());
    }
}

void Duel::spread(double* xs, double* ys) {
    double half = config.arenaSize / 2, margin = 300;
    for (int i = 0; i < config.players; ++i) {
        double best = -1;
        for (int attempt = 0; attempt < 100; ++attempt) {
            double x = uniform(-half + margin, half - margin), y = uniform(-half + margin, half - margin);
            double nearest = std::numeric_limits<double>::infinity();
            for (int j = 0; j < i; ++j) nearest = std::min(nearest, std::hypot(x - xs[j], y - ys[j]));
            if (nearest > best) {
                best = nearest;
                xs[i] = x;
                ys[i] = y;
            }
            if (nearest >= config.spawnMin) break;
        }
    }
}

void Duel::spreadTeams(double* xs, double* ys) {
    int n = config.players;
    double half = config.arenaSize / 2, margin = 300;
    std::vector<int> ids;
    for (int i = 0; i < n; ++i)
        if (std::find(ids.begin(), ids.end(), teamOf(i)) == ids.end()) ids.push_back(teamOf(i));
    int k = (int)ids.size();
    // Each team's centre, far enough in that its tanks fit around it.
    double cx[MAX_PLAYERS] = {}, cy[MAX_PLAYERS] = {};
    double inner = half - margin - config.mateMax / 2;
    if (k == 2) {
        double d = uniform(config.spawnMin, config.spawnMax);
        for (int attempt = 0; attempt < 100; ++attempt) {
            double a = uniform(0, util::PI2);
            cx[0] = std::cos(a) * d / 2;
            cy[0] = std::sin(a) * d / 2;
            if (std::abs(cx[0]) < inner && std::abs(cy[0]) < inner) break;
            d = std::max(config.spawnMin, d * 0.95);
        }
        cx[0] = std::clamp(cx[0], -inner, inner);
        cy[0] = std::clamp(cy[0], -inner, inner);
        cx[1] = -cx[0];
        cy[1] = -cy[0];
    } else {
        for (int t = 0; t < k; ++t) {
            double best = -1;
            for (int attempt = 0; attempt < 100; ++attempt) {
                double x = uniform(-inner, inner), y = uniform(-inner, inner);
                double nearest = std::numeric_limits<double>::infinity();
                for (int j = 0; j < t; ++j) nearest = std::min(nearest, std::hypot(x - cx[j], y - cy[j]));
                if (nearest > best) {
                    best = nearest;
                    cx[t] = x;
                    cy[t] = y;
                }
                if (nearest >= config.spawnMin) break;
            }
        }
    }
    // A team's tanks evenly round its centre, mateMin to mateMax from the next one round.
    for (int t = 0; t < k; ++t) {
        std::vector<int> members;
        for (int i = 0; i < n; ++i)
            if (teamOf(i) == ids[t]) members.push_back(i);
        int m = (int)members.size();
        double gap = uniform(config.mateMin, config.mateMax);
        double r = m > 1 ? gap / (2 * std::sin(M_PI / m)) : 0;
        double start = uniform(0, util::PI2);
        for (int j = 0; j < m; ++j) {
            double a = start + util::PI2 * j / m;
            xs[members[j]] = cx[t] + std::cos(a) * r;
            ys[members[j]] = cy[t] + std::sin(a) * r;
        }
    }
}

void Duel::deal(int slot) {
    Slot& s = slots[slot];
    const std::vector<Choice>& pool = config.choices[slot];
    const Choice* pick = nullptr;
    if (!pool.empty()) {
        double total = 0;
        for (const Choice& c : pool) total += std::max(c.weight, 0.0);
        double r = util::randomFloat() * total;
        pick = &pool.back();
        for (const Choice& c : pool) {
            r -= std::max(c.weight, 0.0);
            if (r < 0) {
                pick = &c;
                break;
            }
        }
    }
    s.tankId = pick ? pick->tank : config.tanks[slot];
    std::copy_n(pick ? pick->stats : config.stats[slot], 8, s.stats);
}

void Duel::spawn(int slot, double x, double y) {
    Client* client = config.kinds[slot] == Human ? clients[slot] : nullptr;
    CameraEntity* cam = client ? (CameraEntity*)client->camera.get() : new CameraEntity(game);
    std::shared_ptr<Inputs> inputs = client ? std::shared_ptr<Inputs>(client->inputs) : std::make_shared<Inputs>();
    TankBody* t = new TankBody(game, cam, inputs);
    cam->cameraData.setPlayer(t);
    if (client) {
        // As the server spawns a player.
        cam->relationsData.setParent(t);
        cam->relationsData.setOwner(t);
        cam->entityState = EntityStateFlags::needsCreate;
        client->camera->spectatee = nullptr;
        client->inputs->isPossessing = false;
        client->inputs->movement.setMagnitude(0);
    }
    if (slots[slot].tankId != Tank::Basic) t->setTank(slots[slot].tankId);
    if (int team = config.teams[slot]; team >= 0) {
        TeamEntity::setTeam(teamEntities[team].get(), t);
        // Before the first tick copies it to them, so stocked drones are on the team too.
        for (Barrel* b : t->barrels) b->relationsData.team = t->relationsData.team;
    }
    cam->setLevel(levelOne(slots[slot].tankId) ? 1 : config.level);
    for (int i = 0; i < StatCount; ++i) {
        cam->setStat(i, std::min((double)slots[slot].stats[i], cam->cameraData.statLimits.values[i]));
    }
    cam->cameraData.setStatsAvailable(0);
    t->healthData.setHealth(t->healthData.maxHealth);
    // No spawn protection: a target that never moves would stay invulnerable.
    t->styleData.setFlags(t->styleData.flags & ~StyleFlags::isFlashing);
    t->damageReduction = 1;
    t->nameData.setName(!names[slot].empty() ? names[slot] : slot == 0 ? "a" : "b");
    t->positionData.setX(x);
    t->positionData.setY(y);
    if (!client) {
        inputs->mouse.x = x + MOUSE;
        inputs->mouse.y = y;
    }

    Slot& s = slots[slot];
    s.tank = t;
    s.camera = cam;
    s.inputs = inputs;
    s.maxHealth = t->healthData.maxHealth;
    s.ballistics.build(t);
    s.mouseDrones = false;
    for (Barrel* b : t->barrels) s.mouseDrones |= b->definition->canControlDrones.value_or(false);
    summarize(slot);
}

void Duel::summarize(int slot) {
    Slot& s = slots[slot];
    float* f = s.summary;
    std::fill(f, f + CLASS_DIM, 0.0f);
    TankBody* t = tank(slot);
    CameraData& cd = t->cameraEntity->cameraData;
    double damage = 7 + 3 * cd.statLevels.values[Stat::BulletDamage];
    double health = 1.5 * cd.statLevels.values[Stat::BulletPenetration] + 2;
    double capacity = 0, total = 0, best = -1, bins[8] = {};
    Barrel* main = t->barrels.empty() ? nullptr : t->barrels[0];
    for (Barrel* b : t->barrels) {
        const BarrelDefinition& def = *b->definition;
        const std::string& type = def.bullet.type;
        if (type == "drone" || type == "necrodrone" || type == "minion") {
            capacity += std::min<double>((double)def.droneCount.value_or(0), 12);
            continue;
        }
        if (type == "swarm") {
            f[2] += 0.25f;
            continue;
        }
        if (type == "trap") f[4] += 0.25f;
        else f[3] += 0.125f;
        // Damage a shot can deal, by how long its bullet lasts, per tick of reload.
        double power = damage * def.bullet.damage * health * def.bullet.health / std::max(b->shootCycle->reloadTime, 1e-6);
        total += power;
        if (power > best) {
            best = power;
            main = b;
        }
        double pos = std::fmod(std::fmod(def.angle, util::PI2) + util::PI2, util::PI2) / (M_PI / 4);
        int k = (int)pos;
        double frac = pos - k;
        bins[k % 8] += power * (1 - frac);
        bins[(k + 1) % 8] += power * frac;
    }
    f[0] = s.mouseDrones ? 1 : 0;
    f[1] = (float)(capacity / 10);
    f[5] = (float)(t->damagePerTick / 20);
    // A basic tank on the glass build is 1.
    f[6] = (float)std::min(total / 43.75, 10.0);
    if (main) {
        const BulletDefinition& def = main->definition->bullet;
        Ballistics flight;
        flight.build(t, main);
        f[7] = (float)(damage * def.damage / 30);
        f[8] = (float)std::min(health * def.health / 20, 10.0);
        f[9] = (float)(main->bulletAccel / 50);
        f[10] = flight.reach.empty() ? 0 : (float)(flight.reach.back() / 3000);
        f[11] = (float)(flight.bulletRadius / 50);
        f[12] = (float)(main->shootCycle->reloadTime / 30);
    }
    f[13] = (float)(960 / cd.FOV / 3000);
    f[14] = (float)(cd.movementSpeed / 3);
    f[15] = (float)(t->healthData.maxHealth / 1000);
    for (int j = 0; j < 8; ++j) f[CLASS_AIM_BINS + j] = total > 0 ? (float)(bins[j] / total) : 0;
}

void Duel::stockDrones(int slot) {
    TankBody* t = tank(slot);
    // Only drones and minions launch on their own. Swarms come only while clicking and soon expire,
    // and their count is unlimited.
    auto stocked = [](Barrel* b) {
        const std::string& type = b->definition->bullet.type;
        return b->definition->droneCount.has_value() && (type == "drone" || type == "minion");
    };
    bool any = false;
    for (Barrel* b : t->barrels) any |= stocked(b);
    if (!any) return;
    // New projectiles go straight into the collision grid, which only accepts them mid-tick. It's
    // cleared and refilled at the start of every tick anyway.
    CollisionManager* grid = game->entities->collisionManager.get();
    grid->preTick(game->tick);
    for (Barrel* b : t->barrels) {
        if (!stocked(b)) continue;
        while (b->droneCount < *b->definition->droneCount) {
            int before = b->droneCount;
            b->shoot();
            if (b->droneCount == before) break;
        }
    }
    grid->postTick(game->tick);
    // Launching kicks the tank back; it should start still.
    t->velocity.x = 0;
    t->velocity.y = 0;
    // Spread evenly around the tank, inside the radius drones rest within.
    std::vector<ObjectEntity*> drones;
    EntityManager* em = game->entities.get();
    for (int id = 0; id <= em->lastId; ++id) {
        Entity* e = em->inner[id];
        if (Entity::exists(e) && dynamic_cast<Drone*>(e) && ((ObjectEntity*)e)->relationsData.owner.get() == (Entity*)t) {
            drones.push_back((ObjectEntity*)e);
        }
    }
    double start = uniform(0, util::PI2);
    for (size_t i = 0; i < drones.size(); ++i) {
        double a = start + util::PI2 * i / drones.size();
        drones[i]->positionData.setX(t->positionData.x + std::cos(a) * 250);
        drones[i]->positionData.setY(t->positionData.y + std::sin(a) * 250);
        drones[i]->velocity.x = 0;
        drones[i]->velocity.y = 0;
    }
}

void Duel::placeShape(int kind, double x, double y) {
    CollisionManager* grid = game->entities->collisionManager.get();
    grid->preTick(game->tick);
    ((DuelArena*)game->arena)->duelShapes()->place(kind, x, y);
    grid->postTick(game->tick);
    record();
}

int Duel::shapeCount() const {
    int n = 0;
    EntityManager* em = game->entities.get();
    for (int id = 0; id <= em->lastId; ++id) {
        Entity* e = em->inner[id];
        n += Entity::exists(e) && (e->entityTags & EntityTags::isShape) ? 1 : 0;
    }
    return n;
}

int Duel::decisionOf(int tick) const { return std::max(0, (tick - 1) / config.repeat); }

int Duel::slotOf(const Entity* e) const {
    for (int k = 0; k < config.players; ++k)
        if (e && e == (const Entity*)tank(k)) return k;
    return -1;
}

int Duel::shooterOf(LivingEntity* source) const {
    int id = source->id;
    if (id < 0 || id >= (int)tracks.size() || tracks[id].hash != source->hash) return -1;
    return tracks[id].shooter;
}

int Duel::aliveCount() const {
    int n = 0;
    for (int i = 0; i < config.players; ++i) n += isAlive(i) ? 1 : 0;
    return n;
}

void Duel::checkOver(int fired) {
    int teamsLeft = 0, last = -1;
    bool learning = false;
    for (int i = 0; i < config.players; ++i) {
        if (!isAlive(i)) continue;
        learning |= config.learner[i];
        bool counted = false;
        for (int j = 0; j < i; ++j) counted |= isAlive(j) && sameTeam(i, j);
        if (!counted) {
            teamsLeft += 1;
            last = teamOf(i);
        }
    }
    if (teamsLeft > 1 && learning) return;
    over = true;
    winner = teamsLeft == 1 ? last : -1;
    killDecision = fired;
}

void Duel::onDamage(LivingEntity* victim, LivingEntity* source, double dealt) {
    if (dealt <= 0 || over) return;
    bool bullet = source->entityTags & EntityTags::isBullet;
    uint64_t key = (uint64_t)(uint32_t)source->id << 32 | source->hash;
    int v = slotOf(victim);
    if (v < 0) {
        // A tank's own shot spent on a shape.
        int k = slotOf(source->relationsData.owner.get());
        if (k >= 0 && bullet && (victim->entityTags & EntityTags::isShape) && !dynamic_cast<Drone*>(source)
            && slots[k].blockedBullets.insert(key).second) {
            slots[k].outcome.shapeBlocked += 1;
        }
        return;
    }
    double frac = dealt / std::max(slots[v].maxHealth, 1.0);
    if (source->entityTags & EntityTags::isShape) {
        slots[v].outcome.taken += (float)frac;
        slots[v].outcome.shapeTaken += (float)frac;
        slots[v].outcome.reward -= (float)(config.takenScale * frac);
        return;
    }
    int a = slotOf(source->topOwner());
    // A shot still flying after its tank died.
    if (a < 0) a = shooterOf(source);
    if (a < 0 || sameTeam(a, v)) return;

    slots[v].outcome.taken += (float)frac;
    slots[v].outcome.reward -= (float)(config.takenScale * frac);
    slots[a].outcome.dealt += (float)frac;
    slots[v].damageFrom[a] += frac;

    if (bullet && slots[a].hitBullets.insert(key).second) slots[a].outcome.hits += 1;
    // Only the tank's own bullets were fired by a decision. Drones launch on their own and minions
    // fire their own bullets, so their hits are paid on impact.
    bool shot = bullet && !dynamic_cast<Drone*>(source) && source->relationsData.owner.get() == (Entity*)tank(a);
    int fired = shot ? decisionOf(((Bullet*)source)->firedTick()) : decision;
    double value = config.dealtScale * frac;
    if (config.creditShots && fired < decision) slots[a].owed.push_back({decision - fired, (float)value});
    else slots[a].outcome.reward += (float)value;

    if (victim->healthData.health <= 0.0001 && !slots[v].dead) {
        slots[v].dead = true;
        kills.push_back({a, v, fired});
        checkOver(fired);
    }
}

void Duel::record() {
    EntityManager* em = game->entities.get();
    int tick = game->tick;
    if ((int)tracks.size() <= em->lastId) tracks.resize(em->lastId + 1);
    for (int id = 0; id <= em->lastId; ++id) {
        Entity* e = em->inner[id];
        if (!Entity::exists(e) || !LivingEntity::isLive(e)) continue;
        ObjectEntity* o = (ObjectEntity*)e;
        Track& t = tracks[id];
        if (t.hash != e->hash) {
            t.hash = e->hash;
            t.first = tick;
            t.shooter = slotOf(o->topOwner());
            if (o->entityTags & EntityTags::isBullet) {
                int k = slotOf(o->relationsData.owner.get());
                if (k >= 0) slots[k].outcome.shots += 1;
            }
        }
        t.x[tick % HISTORY] = (float)o->positionData.x;
        t.y[tick % HISTORY] = (float)o->positionData.y;
    }
}

bool Duel::avgVelocity(int id, int window, double& vx, double& vy) const {
    if (id < 0 || id >= (int)tracks.size()) return false;
    const Track& t = tracks[id];
    int tick = game->tick;
    int span = std::min(window, tick - t.first);
    if (span <= 0) return false;
    int now = tick % HISTORY, then = (tick - span) % HISTORY;
    vx = (t.x[now] - t.x[then]) / span;
    vy = (t.y[now] - t.y[then]) / span;
    return true;
}

void Duel::step(const Action* actions, Outcome* outcomes, std::vector<Credit>& credits) {
    begin(actions);
    for (int r = 0; r < config.repeat; ++r) {
        if (advance()) break;
    }
    settle(outcomes, credits);
}

void Duel::take(int slot, const Action& action) {
    Slot& s = slots[slot];
    s.action = action;
    bool row = s.action.target >= 0 && s.action.target < (int)s.rows.size();
    s.target = row ? s.rows[s.action.target] : Ref<ObjectEntity>();
    if (!row) s.action.target = -1;
}

void Duel::act(int slot, const Action& action) {
    if (config.kinds[slot] == External) take(slot, action);
}

void Duel::begin(const Action* actions, const bool* fresh) {
    int n = config.players;
    kills.clear();
    otherDeaths.clear();
    for (int i = 0; i < n; ++i) {
        Slot& s = slots[i];
        gone[i] = s.dead;
        s.outcome = Outcome{};
        s.owed.clear();
        if (config.kinds[i] != External || (fresh && !fresh[i])) continue;
        take(i, actions[i]);
    }
    for (int i = 0; i < n; ++i) {
        Slot& s = slots[i];
        int k = slotOf(s.target.get());
        if (config.kinds[i] != External || !isAlive(i) || k < 0 || sameTeam(i, k)) continue;
        s.outcome.targeted = true;
        for (int j = 0; j < n; ++j) {
            s.outcome.sameTarget |= j != i && sameTeam(i, j) && isAlive(j) && config.kinds[j] == External
                                    && slots[j].target.get() == s.target.get();
        }
    }
}

bool Duel::advance() {
    int n = config.players;
    for (int i = 0; i < n; ++i) drive(i);
    keepCameras();
    if (recorder) recorder->beforeTick(tanks());
    game->tickLoop();
    for (int i = 0; i < n; ++i) {
        TankBody* t = tank(i);
        if (levelOne(slots[i].tankId) && alive(t) && Entity::exists(t->cameraEntity.get())
            && t->cameraEntity->cameraData.level != 1)
            t->cameraEntity->setLevel(1);
    }
    record();
    if (!over) {
        // Died to something other than a tank, which can only be a shape.
        bool died = false;
        for (int i = 0; i < n; ++i) {
            if (slots[i].dead || alive(tank(i))) continue;
            slots[i].dead = died = true;
            otherDeaths.push_back(i);
        }
        if (died) checkOver(decision);
    }
    return over;
}

bool Duel::settle(Outcome* outcomes, std::vector<Credit>& credits, bool resetWhenEnded) {
    int n = config.players;
    // Deaths after the game was decided, in the same tick, don't count.
    auto standing = [&](int j) { return !slots[j].dead; };
    for (const Kill& k : kills) {
        Slot& killer = slots[k.killer];
        killer.outcome.kills += 1;
        double total = 0;
        for (double d : slots[k.victim].damageFrom) total += d;
        if (slots[k.victim].damageFrom[k.killer] < 0.25 * total) killer.outcome.stolen += 1;
        if (config.killReward != 0) {
            if (config.creditShots && k.fired < decision) killer.owed.push_back({decision - k.fired, (float)config.killReward});
            else killer.outcome.reward += (float)config.killReward;
        }
        for (int j = 0; j < n; ++j) {
            if (j == k.killer || !sameTeam(j, k.killer)) continue;
            if (standing(j)) {
                slots[j].outcome.mateKills += 1;
                slots[j].outcome.reward += (float)config.killReward;
            }
            if (slots[k.victim].damageFrom[j] >= 0.25 * total) slots[j].outcome.assists += 1;
        }
    }
    for (int i = 0; winner >= 0 && i < n; ++i) {
        if (teamOf(i) != winner || !standing(i)) continue;
        Slot& w = slots[i];
        if (config.creditShots && killDecision < decision) w.owed.push_back({decision - killDecision, (float)config.winReward});
        else w.outcome.reward += (float)config.winReward;
        w.outcome.won = true;
    }
    for (const Kill& k : kills) {
        slots[k.victim].outcome.reward -= (float)config.lossReward;
        slots[k.victim].outcome.lost = true;
    }
    for (int i : otherDeaths) {
        slots[i].outcome.reward -= (float)config.lossReward;
        slots[i].outcome.lost = true;
        slots[i].outcome.diedToShape = true;
    }
    bool timedOut = !over && !config.endless && decision + 1 >= config.maxDecisions;
    bool ended = over || timedOut;
    for (int i = 0; i < n; ++i) {
        Slot& s = slots[i];
        int left = 0, mate = -1;
        for (int j = 0; j < n; ++j) {
            if (!sameTeam(i, j) || !standing(j)) continue;
            left += 1;
            if (mate < 0 && j != i) mate = j;
        }
        if (s.outcome.lost && left == 0) {
            s.outcome.eliminated = true;
            s.outcome.reward -= (float)config.eliminatedReward;
        }
        if (s.outcome.lost && !ended) s.outcome.handoff = mate;
        if (ended) {
            s.outcome.teamLeft = left;
            s.outcome.teamWon = winner >= 0 && teamOf(i) == winner;
            s.outcome.teamLost = left == 0;
        }
        outcomes[i] = s.outcome;
        // A tank that died earlier in the game finished its episode then.
        outcomes[i].done = !gone[i] && (ended || s.outcome.lost);
        outcomes[i].timedOut = timedOut && !gone[i] && !s.outcome.lost;
        outcomes[i].firstSeen = s.firstSeen;
        outcomes[i].gameOver = ended;
        if (outcomes[i].timedOut) {
            bool met = false;
            for (int k = 0; k < n; ++k) met |= !sameTeam(i, k) && isAlive(k) && s.sightings[k].decision >= 0;
            outcomes[i].apart = !met;
        }
        for (auto& [ago, value] : s.owed) credits.push_back({i, ago, value});
    }
    decision += 1;
    if (ended && resetWhenEnded) reset();
    return ended;
}

void Duel::coast() {
    for (int i = 0; i < config.players; ++i) {
        if (config.kinds[i] == Human) continue;
        Inputs& in = *slots[i].inputs;
        in.movement.x = 0;
        in.movement.y = 0;
        in.flags = 0;
    }
    keepCameras();
    game->tickLoop();
}

void Duel::drive(int slot) {
    TankBody* me = tank(slot);
    if (!alive(me)) return;
    Slot& s = slots[slot];
    Inputs& in = *s.inputs;
    if (config.kinds[slot] == Human) return;
    if (config.kinds[slot] != External) {
        TankBody* opp = nullptr;
        if (config.players == 2) {
            opp = tank(1 - slot);
        } else {
            double best = std::numeric_limits<double>::infinity();
            for (int k = 0; k < config.players; ++k) {
                if (sameTeam(k, slot) || !isAlive(k)) continue;
                double d = std::hypot(tank(k)->positionData.x - me->positionData.x, tank(k)->positionData.y - me->positionData.y);
                if (d < best) {
                    best = d;
                    opp = tank(k);
                }
            }
        }
        driveScripted(slot, me, opp);
        return;
    }

    const Action& a = s.action;
    Frame frame(s.frame);
    in.movement.x = 0;
    in.movement.y = 0;
    if (a.move > 0) {
        double m = frame.toWorld((a.move - 1) * M_PI / 4);
        in.movement.x = std::cos(m);
        in.movement.y = std::sin(m);
    }
    in.flags = (a.fire ? InputFlags::leftclick : 0) | (a.repel ? InputFlags::rightclick : 0);

    bool placed = !std::isnan(a.reach);
    double angle = frame.toWorld(a.aim);
    double reach = placed ? std::exp((double)a.reach) : MOUSE;
    if (a.target >= 0) {
        ObjectEntity* t = s.target.get();
        if (t && !t->deletionAnimation) {
            double dx = t->positionData.x - me->positionData.x, dy = t->positionData.y - me->positionData.y;
            angle = std::atan2(dy, dx) + frame.lead(a.aim);
            if (placed) reach *= std::hypot(dx, dy);
        } else {
            angle = s.heldAngle;
            reach = s.heldReach;
        }
    }
    // Never on the tank's centre, where its facing would snap to angle 0.
    if (placed) reach = std::clamp(reach, me->physicsData.size, MAX_REACH);
    s.heldAngle = angle;
    s.heldReach = reach;
    in.mouse.x = me->positionData.x + std::cos(angle) * reach;
    in.mouse.y = me->positionData.y + std::sin(angle) * reach;
}

void Duel::driveScripted(int slot, TankBody* me, TankBody* opp) {
    Slot& s = slots[slot];
    Inputs& in = *s.inputs;
    int kind = config.kinds[slot];
    int tick = game->tick;
    double x = me->positionData.x, y = me->positionData.y;
    // Tanks can go past the arena's edge into the padding, so steer off the real limit.
    double half = config.arenaSize / 2 + game->arena->ARENA_PADDING;
    double vx = 0, vy = 0;
    bool fire = false;
    double aimX = in.mouse.x, aimY = in.mouse.y;

    if (kind == Linear) {
        double dx = std::cos(s.linearAngle), dy = std::sin(s.linearAngle);
        if ((x < -half + 250 && dx < 0) || (x > half - 250 && dx > 0)) dx = -dx;
        if ((y < -half + 250 && dy < 0) || (y > half - 250 && dy > 0)) dy = -dy;
        s.linearAngle = std::atan2(dy, dx);
        vx = dx;
        vy = dy;
    } else if ((kind == Strafe || kind == Fighter) && alive(opp)) {
        double ox = opp->positionData.x - x, oy = opp->positionData.y - y;
        double d = std::max(std::hypot(ox, oy), 1.0);
        double ux = ox / d, uy = oy / d;
        if (tick >= s.nextFlip) {
            s.strafeDir = -s.strafeDir;
            s.nextFlip = tick + 12 + (int)(util::randomFloat() * 50);
        }
        double radial = d > 1000 ? 1 : d < 550 ? -1 : 0;
        vx = radial * ux - s.strafeDir * uy * 0.9;
        vy = radial * uy + s.strafeDir * ux * 0.9;

        double m = 350;
        if (x < -half + m) vx += 1.5 * (1 - (x + half) / m);
        if (x > half - m) vx -= 1.5 * (1 - (half - x) / m);
        if (y < -half + m) vy += 1.5 * (1 - (y + half) / m);
        if (y > half - m) vy -= 1.5 * (1 - (half - y) / m);

        if (kind == Fighter) {
            if (config.fighterDodges) {
                EntityManager* em = game->entities.get();
                for (int id = 0; id <= em->lastId; ++id) {
                    Entity* e = em->inner[id];
                    if (!Entity::exists(e) || !(e->entityTags & EntityTags::isBullet)) continue;
                    ObjectEntity* b = (ObjectEntity*)e;
                    if (b->deletionAnimation || b->relationsData.team.sameAs(me->relationsData.team)) continue;
                    double rx = b->positionData.x - x, ry = b->positionData.y - y;
                    double qx = b->velocity.x - me->velocity.x, qy = b->velocity.y - me->velocity.y;
                    double qq = qx * qx + qy * qy;
                    if (qq < 1) continue;
                    double t = -(rx * qx + ry * qy) / qq;
                    if (t < 0 || t > 20) continue;
                    double cx = rx + qx * t, cy = ry + qy * t;
                    double dmin = std::hypot(cx, cy);
                    if (dmin > me->physicsData.size + b->physicsData.size + 30) continue;
                    if (dmin < 1) {
                        cx = -qy;
                        cy = qx;
                        dmin = std::hypot(cx, cy);
                    }
                    double w = (1 - t / 20) * 3;
                    vx -= cx / dmin * w;
                    vy -= cy / dmin * w;
                }
            }
            double ax, ay, ticks;
            s.ballistics.intercept(x + me->velocity.x, y + me->velocity.y, opp->positionData.x, opp->positionData.y,
                                   opp->velocity.x, opp->velocity.y, ax, ay, ticks);
            if (tick >= s.noiseUntil) {
                s.noise = gauss() * config.aimNoise;
                s.noiseUntil = tick + 8;
            }
            double angle = std::atan2(ay - y, ax - x) + s.noise;
            // Drones go to the mouse itself, so it goes on the opponent.
            double reach = s.mouseDrones ? d : MOUSE;
            aimX = x + std::cos(angle) * reach;
            aimY = y + std::sin(angle) * reach;
            fire = config.fighterShoots;
        } else {
            aimX = opp->positionData.x;
            aimY = opp->positionData.y;
        }
    }

    int move = quantize(vx, vy);
    in.movement.x = 0;
    in.movement.y = 0;
    if (move > 0) {
        double m = (move - 1) * M_PI / 4;
        in.movement.x = std::cos(m);
        in.movement.y = std::sin(m);
    }
    in.flags = fire ? InputFlags::leftclick : 0;
    in.mouse.x = aimX;
    in.mouse.y = aimY;
}

int Duel::observe(int slot, float* self, float* ents, uint8_t* mask, uint8_t* targetable, int* seen) {
    std::memset(self, 0, SELF_DIM * sizeof(float));
    *seen = 0;
    Slot& s = slots[slot];
    s.rows.clear();
    TankBody* me = tank(slot);
    // A dead tank's camera goes with it.
    if (!isAlive(slot) || !Entity::exists(me->cameraEntity)) return 0;

    CameraData& cd = me->cameraEntity->cameraData;
    ArenaData& ad = game->arena->arenaData;
    int tick = game->tick;
    double x = me->positionData.x, y = me->positionData.y;
    double mvx = me->velocity.x, mvy = me->velocity.y;
    // What a player's 1920x1080 screen shows.
    double hw = 960 / cd.FOV, hh = 540 / cd.FOV;
    Frame frame(s.frame);

    double fvx = mvx, fvy = mvy;
    frame.vec(fvx, fvy);
    self[0] = (float)(fvx / VEL);
    self[1] = (float)(fvy / VEL);
    for (int w = 0; w < 3; ++w) {
        double ax = mvx, ay = mvy;
        avgVelocity(me->id, WINDOWS[w], ax, ay);
        frame.vec(ax, ay);
        self[2 + 2 * w] = (float)(ax / VEL);
        self[3 + 2 * w] = (float)(ay / VEL);
    }
    self[8] = (float)(me->healthData.health / std::max(me->healthData.maxHealth, 1.0));
    if (!me->barrels.empty()) {
        ShootCycle* c = me->barrels[0]->shootCycle.get();
        self[9] = clampf(c->pos / std::max(c->reloadTime, 1e-6), 0, 1);
    }
    // Distances to how far a tank can actually go, past the edge into the padding. The arena is
    // centred on 0, so turning the position turns the walls with it.
    double fx = x, fy = y;
    frame.vec(fx, fy);
    double pad = game->arena->ARENA_PADDING;
    self[10] = clampf((fx - ad.leftX + pad) / 2000, 0, 1);
    self[11] = clampf((ad.rightX + pad - fx) / 2000, 0, 1);
    self[12] = clampf((fy - ad.topY + pad) / 2000, 0, 1);
    self[13] = clampf((ad.bottomY + pad - fy) / 2000, 0, 1);
    int clock = config.endless ? std::min(decision, config.maxDecisions - 1) : decision;
    self[14] = (float)clock / std::max(config.maxDecisions, 1);
    const Action& last = s.action;
    self[15 + std::clamp(last.move, 0, MOVES - 1)] = 1;
    self[24] = last.fire ? 1 : 0;
    self[25] = last.target >= 0 ? 1 : 0;
    self[26] = (float)std::sin(last.aim);
    self[27] = (float)std::cos(last.aim);
    self[28] = (float)std::sin(frame.angle(me->positionData.angle));
    self[29] = (float)std::cos(frame.angle(me->positionData.angle));
    self[30] = (float)(me->physicsData.size / 100);

    static thread_local std::vector<std::pair<double, ObjectEntity*>> near;
    near.clear();
    EntityManager* em = game->entities.get();
    int ownBullets = 0;
    for (int id = 0; id <= em->lastId; ++id) {
        Entity* e = em->inner[id];
        if (!Entity::exists(e) || !LivingEntity::isLive(e) || e == (Entity*)me) continue;
        ObjectEntity* o = (ObjectEntity*)e;
        if (o->deletionAnimation) continue;
        if (o->relationsData.owner.get() == (Entity*)me) ownBullets += 1;
        double dx = o->positionData.x - x, dy = o->positionData.y - y;
        double r = o->physicsData.size;
        double sx = dx, sy = dy;
        frame.vec(sx, sy);
        if (std::abs(sx) > hw + r || std::abs(sy) > hh + r) continue;
        near.push_back({dx * dx + dy * dy, o});
    }
    self[31] = (float)ownBullets / 10;

    bool shown[MAX_PLAYERS] = {};
    for (auto& [d2, o] : near) {
        if (!(o->entityTags & EntityTags::isTank)) continue;
        int k = slotOf(o);
        if (k < 0 || k == slot) continue;
        TankBody* other = tank(k);
        if (s.firstSeen < 0 && !sameTeam(slot, k)) s.firstSeen = decision;
        shown[k] = true;
        s.sightings[k] = {decision, o->positionData.x, o->positionData.y, o->velocity.x, o->velocity.y,
                          o->positionData.angle, other->healthData.health / std::max(other->healthData.maxHealth, 1.0)};
    }
    // Where on the map I am, and where I last saw an enemy: the one seen most recently, the nearest of those.
    double half = std::max(config.arenaSize / 2, 1.0);
    self[32] = (float)(fx / half);
    self[33] = (float)(fy / half);
    const Sighting* latest = nullptr;
    int enemies = 0, mates = 0, matesLeft = 0;
    for (int k = 0; k < config.players; ++k) {
        if (k != slot && sameTeam(k, slot)) {
            mates += 1;
            matesLeft += isAlive(k) ? 1 : 0;
            continue;
        }
        if (k == slot || !isAlive(k)) continue;
        enemies += 1;
        const Sighting& g = s.sightings[k];
        if (g.decision < 0) continue;
        if (!latest || g.decision > latest->decision
            || (g.decision == latest->decision && std::hypot(g.x - x, g.y - y) < std::hypot(latest->x - x, latest->y - y))) {
            latest = &g;
        }
    }
    self[ENEMIES_LEFT] = (float)enemies / 3;
    self[MATES] = (float)mates / 3;
    self[MATES_LEFT] = (float)matesLeft / 3;
    if (latest) {
        double lx = latest->x - x, ly = latest->y - y, lvx = latest->vx, lvy = latest->vy;
        frame.vec(lx, ly);
        frame.vec(lvx, lvy);
        self[34] = (float)(lx / POS);
        self[35] = (float)(ly / POS);
        self[36] = (float)(lvx / VEL);
        self[37] = (float)(lvy / VEL);
        self[38] = (float)latest->health;
        self[39] = (float)std::min(decision - latest->decision, SEEN_CAP) / SEEN_CAP;
    } else {
        self[39] = 1;
        self[40] = 1;
    }
    // Barrels sit at angles from the facing, which mirroring turns the other way.
    auto putClass = [&](const float* from, float* to) {
        std::copy_n(from, CLASS_AIM_BINS, to);
        for (int j = 0; j < 8; ++j) to[CLASS_AIM_BINS + j] = from[CLASS_AIM_BINS + (frame.flip ? (8 - j) % 8 : j)];
    };
    putClass(s.summary, self + OLD_SELF_DIM);

    // Tanks off screen, where they were last seen, unless that spot is on screen now.
    static thread_local std::vector<std::pair<double, int>> ghosts;
    ghosts.clear();
    for (int k = 0; config.remember[slot] && k < config.players; ++k) {
        const Sighting& g = s.sightings[k];
        if (k == slot || shown[k] || g.decision < 0 || !isAlive(k)) continue;
        double sx = g.x - x, sy = g.y - y;
        frame.vec(sx, sy);
        double r = tank(k)->physicsData.size;
        if (std::abs(sx) <= hw + r && std::abs(sy) <= hh + r) continue;
        ghosts.push_back({std::hypot(g.x - x, g.y - y), k});
    }
    std::sort(ghosts.begin(), ghosts.end());
    size_t cap = (size_t)std::clamp(config.entityCap, 0, ENT_MAX);
    if (ghosts.size() > cap) ghosts.resize(cap);

    *seen = (int)near.size();
    size_t n = std::min(near.size(), cap - ghosts.size());
    std::memset(ents, 0, (n + ghosts.size()) * ENT_DIM * sizeof(float));
    // Shapes after everything else, so a crowded screen only ever loses the farthest shapes.
    auto isShape = [](const ObjectEntity* o) { return (o->entityTags & EntityTags::isShape) != 0; };
    std::partial_sort(near.begin(), near.begin() + n, near.end(), [&](const auto& a, const auto& b) {
        bool sa = isShape(a.second), sb = isShape(b.second);
        return sa != sb ? sb : a.first < b.first;
    });
    for (size_t i = 0; i < n; ++i) {
        ObjectEntity* o = near[i].second;
        float* f = ents + i * ENT_DIM;
        double dx = o->positionData.x - x, dy = o->positionData.y - y;
        double dist = std::sqrt(near[i].first);
        double ovx = o->velocity.x, ovy = o->velocity.y;
        double qx = ovx - mvx, qy = ovy - mvy;
        double tdx = dx, tdy = dy, tvx = ovx, tvy = ovy, tqx = qx, tqy = qy;
        frame.vec(tdx, tdy);
        frame.vec(tvx, tvy);
        frame.vec(tqx, tqy);
        f[0] = (float)(tdx / POS);
        f[1] = (float)(tdy / POS);
        f[2] = (float)(dist / POS);
        f[3] = (float)(tvx / VEL);
        f[4] = (float)(tvy / VEL);
        f[5] = (float)(tqx / VEL);
        f[6] = (float)(tqy / VEL);
        for (int w = 0; w < 3; ++w) {
            double ax = ovx, ay = ovy;
            avgVelocity(o->id, WINDOWS[w], ax, ay);
            frame.vec(ax, ay);
            f[7 + 2 * w] = (float)(ax / VEL);
            f[8 + 2 * w] = (float)(ay / VEL);
        }
        int age = o->id < (int)tracks.size() ? tick - tracks[o->id].first : 0;
        f[13] = (float)std::min(age, 16) / 16;
        f[14] = (float)(o->physicsData.size / 100);
        HealthData* hd = o->getHealthData();
        f[15] = (float)(hd->health / std::max(hd->maxHealth, 1.0));
        f[16] = (float)std::sin(frame.angle(o->positionData.angle));
        f[17] = (float)std::cos(frame.angle(o->positionData.angle));
        bool isTank = o->entityTags & EntityTags::isTank;
        bool isBullet = o->entityTags & EntityTags::isBullet;
        // Shapes are on nobody's side.
        bool shape = isShape(o);
        f[18] = isTank;
        f[19] = isBullet;
        f[20] = !shape && ((!isTank && !isBullet) || dynamic_cast<Drone*>(o));
        bool enemy = !shape && !o->relationsData.team.sameAs(me->relationsData.team);
        f[21] = o->relationsData.owner.get() == (Entity*)me;
        f[22] = enemy;
        f[ALLY] = !shape && !enemy && o->topOwner() != (ObjectEntity*)me;
        if (isBullet) {
            Bullet* b = (Bullet*)o;
            f[23] = clampf((tick - b->firedTick()) / std::max(b->lifeTicks(), 1.0), 0, 1);
        }
        // Closest approach to me if both keep their current velocities.
        double qq = qx * qx + qy * qy;
        double t = qq > 1e-6 ? std::max(0.0, -(dx * qx + dy * qy) / qq) : 0;
        double cx = dx + qx * t, cy = dy + qy * t;
        f[24] = clampf(t / 25, 0, 2);
        f[25] = clampf(std::hypot(cx, cy) / 500, 0, 2);
        f[26] = qq > 1e-6 && dist > 1 ? (float)(-(dx * qx + dy * qy) / (std::sqrt(qq) * dist)) : 0;
        f[27] = (float)(hd->maxHealth / 1000);
        for (int k = 0; k < config.players; ++k) {
            if (o == (ObjectEntity*)tank(k)) putClass(slots[k].summary, f + OLD_ENT_DIM);
        }
        if (shape) {
            float* kind = f + CLASS_ENT_DIM;
            kind[0] = 1;
            // Squares 0.29, triangles 0.42, pentagons 0.57, alpha pentagons 0.99.
            kind[1] = clampf(std::log10(std::max(hd->maxHealth, 1.0)) / 3.5, 0, 1.5);
            kind[2] = dynamic_cast<Crasher*>(o) ? 1 : 0;
            // A shape's spin means nothing.
            f[16] = f[17] = 0;
        }
        mask[i] = 1;
        targetable[i] = enemy && isTank;
        s.rows.push_back(o);
    }
    for (size_t j = 0; j < ghosts.size(); ++j) {
        int k = ghosts[j].second;
        const Sighting& g = s.sightings[k];
        TankBody* t = tank(k);
        float* f = ents + (n + j) * ENT_DIM;
        double dx = g.x - x, dy = g.y - y, gvx = g.vx, gvy = g.vy, qx = g.vx - mvx, qy = g.vy - mvy;
        frame.vec(dx, dy);
        frame.vec(gvx, gvy);
        frame.vec(qx, qy);
        f[0] = (float)(dx / POS);
        f[1] = (float)(dy / POS);
        f[2] = (float)(ghosts[j].first / POS);
        f[3] = (float)(gvx / VEL);
        f[4] = (float)(gvy / VEL);
        f[5] = (float)(qx / VEL);
        f[6] = (float)(qy / VEL);
        for (int w = 0; w < 3; ++w) {
            f[7 + 2 * w] = (float)(gvx / VEL);
            f[8 + 2 * w] = (float)(gvy / VEL);
        }
        f[13] = 1;
        f[14] = (float)(t->physicsData.size / 100);
        f[15] = (float)g.health;
        f[16] = (float)std::sin(frame.angle(g.angle));
        f[17] = (float)std::cos(frame.angle(g.angle));
        f[18] = 1;
        f[22] = !sameTeam(slot, k);
        f[ALLY] = sameTeam(slot, k);
        // Where it was heading is stale, so nothing about it is coming at me.
        f[27] = (float)(t->healthData.maxHealth / 1000);
        putClass(slots[k].summary, f + OLD_ENT_DIM);
        f[REMEMBERED] = 1;
        f[SINCE_SEEN] = (float)std::min(decision - g.decision, SEEN_CAP) / SEEN_CAP;
        mask[n + j] = 1;
        targetable[n + j] = 0;
        s.rows.push_back(Ref<ObjectEntity>());
    }
    return (int)(n + ghosts.size());
}

}
