#pragma once

#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

#include "Ballistics.h"
#include "Recorder.h"
#include "Native/Ref.h"

class Client;
class GameServer;
class Entity;
class TankBody;
class ObjectEntity;
class CameraEntity;
class Inputs;
class LivingEntity;

namespace bots {

// What a tank's class and build can do: guns, bullets, drones, speed and view. Shown for the tank
// itself after the older self features, and for each tank on screen after the older entity features.
constexpr int CLASS_DIM = 24;
constexpr int OLD_SELF_DIM = 41;
constexpr int OLD_ENT_DIM = 28;
// What kind of thing an entity is, after its class features: a shape, how tough a shape is, a crasher,
// and an enemy tank off screen, shown where it was last seen.
constexpr int KIND_DIM = 4;
// How many enemy tanks are still alive, after the class features of the tank itself; then how many teammates it
// started with, and how many of them are still alive.
constexpr int ENEMIES_LEFT = OLD_SELF_DIM + CLASS_DIM;
constexpr int MATES = ENEMIES_LEFT + 1;
constexpr int MATES_LEFT = MATES + 1;
constexpr int SELF_DIM = MATES_LEFT + 1;
// How much of each entity the bots before shapes were shown.
constexpr int CLASS_ENT_DIM = OLD_ENT_DIM + CLASS_DIM;
constexpr int REMEMBERED = CLASS_ENT_DIM + 3;
// How long ago a remembered tank was seen, after the kind; 0 for everything on screen.
constexpr int SINCE_SEEN = CLASS_ENT_DIM + KIND_DIM;
// A teammate's tank, or anything it fired or launched.
constexpr int ALLY = SINCE_SEEN + 1;
constexpr int ENT_DIM = ALLY + 1;
// Where the class features' firepower by direction starts, eight 45 degree bins from the tank's facing.
constexpr int CLASS_AIM_BINS = 16;
// Most entity rows a tank can be shown. Even a crowded screen stays well under it.
constexpr int ENT_MAX = 256;
constexpr int MOVES = 9;
// Ticks of position history kept per entity; enough for a 16 tick window.
constexpr int HISTORY = 17;
// Most tanks a game can have.
constexpr int MAX_PLAYERS = 8;

// Who drives a tank. External tanks take actions from Python, Human ones a seated client's inputs; the rest are
// scripted.
enum Kind { External = 0, Still = 1, Linear = 2, Strafe = 3, Fighter = 4, Human = 5 };

// A class and build a slot can be dealt at the start of a game, by weight.
struct Choice {
    int tank = 0;
    int stats[8] = {5, 7, 6, 6, 5, 0, 4, 0};
    double weight = 1;
};

struct Config {
    Config();
    // Tanks in a game. Past two without teams it's a free-for-all, won by the last one left.
    int players = 2;
    // Each slot's team, from 0; -1 is on its own. Teammates can't hurt each other, and the game is won by the last
    // team with a tank left.
    int teams[MAX_PLAYERS];
    double arenaSize = 3000;
    int maxDecisions = 375;
    // Play on past maxDecisions without timing out; the time tanks are shown stops a decision short of it.
    bool endless = false;
    // Ticks each decision is held for.
    int repeat = 4;
    int level = 45;
    // Tank class of each slot.
    int tanks[MAX_PLAYERS] = {};
    // Stat points of each slot, in server order (movement first, regen last).
    int stats[MAX_PLAYERS][8];
    // Two tanks start spawnMin to spawnMax apart; more start anywhere, at least spawnMin from each other.
    double spawnMin = 600, spawnMax = 1200;
    // Spawn the two tanks mirrored through the arena's centre, spawnMin to spawnMax apart.
    bool mirrorSpawn = false;
    // With two teams, their centres start mirrored through the arena's centre, spawnMin to spawnMax apart, and each
    // team's tanks mateMin to mateMax from each other.
    double mateMin = 300, mateMax = 600;
    int kinds[MAX_PLAYERS];
    // The tanks left on the last team standing get winReward, a tank that dies loses lossReward, and its killer
    // and the killer's teammates still alive get killReward. The last of a team to die loses eliminatedReward more.
    double winReward = 1, lossReward = 1, killReward = 0, eliminatedReward = 0;
    // Per whole health bar of damage.
    double dealtScale = 0.5, takenScale = 0.5;
    // Pay a hit, and the kill, on the decision that fired the bullet instead of on impact.
    bool creditShots = true;
    // Scripted fighter: aim error in radians, and whether it shoots and dodges.
    double aimNoise = 0;
    bool fighterShoots = true, fighterDodges = true;
    // Also report scripted tanks as rows (their actions are ignored), for watching them.
    bool exposeScripted = false;
    // Show each external tank its episode turned by one of the square arena's 8 symmetries,
    // picked at random per episode, so every fight teaches all 8 orientations.
    bool symmetry = false;
    // Drone tanks start with their full set of drones out, as they'd arrive at a real fight.
    bool stockDrones = true;
    // How many of the nearest on-screen entities each tank is shown. The first bots trained with 40.
    int entityCap = ENT_MAX;
    // When a slot has choices, each game deals it one of them instead of tanks and stats.
    std::vector<Choice> choices[MAX_PLAYERS];
    // Slots shown each enemy tank they've seen, while it's off screen, where they last saw it.
    bool remember[MAX_PLAYERS] = {};
    // Slots whose decisions are learned from: once none of them is alive, the game ends.
    bool learner[MAX_PLAYERS];
    // Shapes, as a multiple of FFA's density: each game draws one from shapesMin to shapesMax. shapesEmpty of
    // the games have none, and pentagonRich of the rest are 20 to 40% pentagons instead of 4%.
    double shapesMin = 0, shapesMax = 0;
    double shapesEmpty = 0, pentagonRich = 0;
};

// move: 0 still, 1..8 = direction (move - 1) * 45 degrees. target: an entity row of the last
// observation to aim at with aim as the lead angle added to its direction, or -1 to aim at the
// absolute angle aim. fire holds left click and repel right click.
// reach places the mouse: with a target, the log of its distance as a multiple of the target's;
// without, the log of its distance. NaN puts it far away, which is all bullets need.
struct Action {
    int move = 0;
    bool fire = false;
    int target = -1;
    float aim = 0;
    bool repel = false;
    float reach = std::numeric_limits<float>::quiet_NaN();
};

// What one decision did for a tank. A tank's episode is done when it dies or its game ends; a dead
// tank's game can go on without it.
struct Outcome {
    float reward = 0;
    bool done = false, won = false, lost = false, timedOut = false;
    // Tanks it killed, and of those, the ones it had dealt under a quarter of the damage to.
    int kills = 0, stolen = 0;
    // Its game ran out of time with it alive and no other tank still alive ever on its screen.
    bool apart = false;
    // Its game ended this decision, with it alive or not.
    bool gameOver = false;
    // At its game's end, whether its team won or had no tank left, and how many of its team were still alive.
    bool teamWon = false, teamLost = false;
    int teamLeft = 0;
    // It died this decision as the last of its team, or with a teammate alive, the slot of the one whose future
    // its own carries on in; -1 otherwise.
    bool eliminated = false;
    int handoff = -1;
    // Kills by its teammates it was paid for; and of all its team's kills, those of a tank it had dealt at least a
    // quarter of the damage to without the kill.
    int mateKills = 0, assists = 0;
    // It aimed at a tank, and a teammate aimed at the same one.
    bool targeted = false, sameTarget = false;
    // In whole health bars.
    float dealt = 0, taken = 0;
    int shots = 0, hits = 0;
    // Decision an enemy tank first came on screen this episode, or -1.
    int firstSeen = -1;
    // Damage from shapes, also counted in taken; the tank's own shots that hit a shape; and whether a shape
    // gave it its death.
    float shapeTaken = 0;
    int shapeBlocked = 0;
    bool diedToShape = false;
};

// Reward owed to a decision stepsAgo decisions before the one just run.
struct Credit {
    int slot;
    int stepsAgo;
    float value;
};

struct Track {
    uint32_t hash = 0;
    int first = 0;
    // The slot whose tank fired it, which still counts once the tank is gone; -1 for anything else.
    int shooter = -1;
    float x[HISTORY], y[HISTORY];
};

// An enemy tank as it was the last time it was on a tank's screen.
struct Sighting {
    int decision = -1;
    double x = 0, y = 0, vx = 0, vy = 0, angle = 0, health = 0;
};

class Duel {
public:
    explicit Duel(const Config& config);
    // Builds no game yet: seat() the Human slots, then reset().
    struct Deferred {};
    Duel(const Config& config, Deferred);
    ~Duel();
    Duel(const Duel&) = delete;
    Duel& operator=(const Duel&) = delete;

    Config config;

    // Starts a new episode in a fresh game.
    void reset();
    // Runs one decision. actions has one entry per slot (scripted slots ignore theirs).
    // Finished episodes reset, so the next observation is the new episode's first.
    void step(const Action* actions, Outcome* outcomes, std::vector<Credit>& credits);
    // step() a tick at a time: begin, then advance up to repeat times, stopping early once it returns true (the
    // game is decided), then settle, which returns whether the game ended. Slots with fresh false keep the action
    // they had, aimed at the same thing.
    void begin(const Action* actions, const bool* fresh = nullptr);
    bool advance();
    bool settle(Outcome* outcomes, std::vector<Credit>& credits, bool resetWhenEnded = true);
    // A new action for one slot between advances, its target a row of the slot's last observation.
    void act(int slot, const Action& action);
    // A tick of an ended game, with every tank still.
    void coast();
    // A Human slot's client, which reset() moves into each new game and gives the slot's tank.
    void seat(int slot, Client* client);
    // Tank names by slot; empty ones are "a" for slot 0 and "b" for the rest.
    std::string names[MAX_PLAYERS];
    // The team that won the game just ended, as teamOf numbers it, or -1.
    int winningTeam() const { return winner; }
    // Writes up to entityCap entity rows and returns how many; seen gets how many were on screen.
    int observe(int slot, float* self, float* ents, uint8_t* mask, uint8_t* targetable, int* seen);
    // Its tank is alive and in the game.
    bool isAlive(int slot) const;
    // On one team: the same slot, or both on the same team.
    bool sameTeam(int a, int b) const;

    GameServer* game = nullptr;
    int decision = 0;

    struct Slot {
        // The class and build this game dealt the slot.
        int tankId = 0;
        int stats[8] = {};
        float summary[CLASS_DIM] = {};

        Ref<TankBody> tank;
        Ref<CameraEntity> camera;
        std::shared_ptr<Inputs> inputs;
        Ballistics ballistics;
        double maxHealth = 1;

        Action action;
        Ref<ObjectEntity> target;
        double heldAngle = 0;
        double heldReach = 0;
        // Its drones fly to the mouse while it clicks.
        bool mouseDrones = false;
        // The entities behind the rows of the last observation.
        std::vector<Ref<ObjectEntity>> rows;

        int strafeDir = 1;
        int nextFlip = 0;
        double linearAngle = 0;
        double noise = 0;
        int noiseUntil = 0;

        // Which of the 8 symmetries this tank sees: rotation frame % 4 quarter turns, after
        // mirroring y when frame >= 4. Play deals only the even ones, which keep the screen wide.
        int frame = 0;

        // Each enemy tank as it was the last time it was on screen.
        int firstSeen = -1;
        Sighting sightings[MAX_PLAYERS];
        // Dead this game, and the damage each slot dealt it, in whole health bars.
        bool dead = false;
        double damageFrom[MAX_PLAYERS] = {};

        Outcome outcome;
        std::vector<std::pair<int, float>> owed;
        // Bullets that already counted as a hit this episode, as id << 32 | hash.
        std::unordered_set<uint64_t> hitBullets;
        // And those that hit a shape.
        std::unordered_set<uint64_t> blockedBullets;
    };
    Slot slots[MAX_PLAYERS];

    TankBody* tank(int slot) const;
    void setFrame(int slot, int frame) { slots[slot].frame = frame & 7; }
    // Puts a shape at (x, y), for tests: 3 triangle, 4 square, 5 pentagon, 6 alpha pentagon.
    void placeShape(int kind, double x, double y);
    int shapeCount() const;
    // Records what server clients watching it would get, from now on, game after game. names are
    // the tanks', by slot.
    void startReplay(const std::vector<ReplayView>& views, const std::vector<std::string>& names);
    std::vector<Packet> takePackets();

private:
    std::vector<Track> tracks;
    std::unique_ptr<Recorder> recorder;
    // The game is decided, or ended without anyone left to learn from; winner is the team of the
    // last ones left, if any.
    bool over = false;
    int winner = -1;
    // The server's team entity of each team.
    Ref<Entity> teamEntities[MAX_PLAYERS];
    Client* clients[MAX_PLAYERS] = {};
    // Slots already dead when the decision being run began.
    bool gone[MAX_PLAYERS] = {};
    int killDecision = -1;
    // Kills during the decision being run: killer, victim, and the decision that fired the killing shot.
    struct Kill {
        int killer, victim, fired;
    };
    std::vector<Kill> kills;
    // Tanks that died to something other than a tank during the decision being run.
    std::vector<int> otherDeaths;

    void take(int slot, const Action& action);
    void deal(int slot);
    void spawn(int slot, double x, double y);
    void summarize(int slot);
    void stockDrones(int slot);
    void drive(int slot);
    void driveScripted(int slot, TankBody* me, TankBody* opp);
    // Places the tanks of a game with more than two, or with teams.
    void spread(double* xs, double* ys);
    void spreadTeams(double* xs, double* ys);
    int teamOf(int slot) const;
    bool hasTeams() const;
    // Who's alive, and whether the game is over because at most one team has a tank left or no learner does.
    int aliveCount() const;
    void checkOver(int fired);
    std::vector<TankBody*> tanks() const;
    int shooterOf(LivingEntity* source) const;
    void record();
    void onDamage(LivingEntity* victim, LivingEntity* source, double dealt);
    // The slot whose tank this is, or -1.
    int slotOf(const Entity* e) const;
    int decisionOf(int tick) const;
    bool avgVelocity(int id, int window, double& vx, double& vy) const;
    void destroyGame();
    void holdForReplay();
    void keepCameras();
};

}
