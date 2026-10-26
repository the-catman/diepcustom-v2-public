#include "Const/Commands.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include "Client.h"
#include "Config.h"
#include "Game.h"
#include "Util.h"
#include "Native/Arena.h"
#include "Native/Camera.h"
#include "Native/Entity.h"
#include "Native/Manager.h"
#include "Const/Enums.h"
#include "Const/TankDefinitions.h"
#include "Entity/Object.h"
#include "Entity/Live.h"
#include "Entity/AI.h"
#include "Entity/Tank/TankBody.h"
#include "Entity/Boss/AbstractBoss.h"
#include "Entity/Misc/ArenaCloser.h"
#include "Entity/Misc/TeamEntity.h"
#include "Entity/Misc/Dominator.h"
#include "Entity/Misc/Mothership.h"
#include "Entity/Shape/AbstractShape.h"
#include "Entity/Shape/Crasher.h"
#include "Entity/Shape/Pentagon.h"
#include "Entity/Shape/Square.h"
#include "Entity/Shape/Triangle.h"
#include "Entity/Tank/AutoTurret.h"
#include "Entity/Tank/Projectile/Bullet.h"
#include "Entity/Boss/Defender.h"
#include "Entity/Boss/FallenBooster.h"
#include "Entity/Boss/FallenOverlord.h"
#include "Entity/Boss/Guardian.h"
#include "Entity/Boss/Summoner.h"
#include "Entity/Misc/Boss/FallenAC.h"
#include "Entity/Misc/Boss/FallenSpike.h"
#include "Entity/Misc/Boss/FallenMegaTrapper.h"

// ---- helpers ----

static bool isRelativeArg(const std::string& s) {
    return !s.empty() && s.front() == '~';
}

static double parseOrNaN(const std::string& s) {
    // Match JS parseInt(s, 10): leading integer, no int32 overflow throw, NaN if no digits.
    const char* p = s.c_str();
    char* end = nullptr;
    long long v = std::strtoll(p, &end, 10);
    if (end == p) return std::numeric_limits<double>::quiet_NaN();
    return (double)v;
}

static double resolveRelative(const std::string& arg, double base) {
    std::string rest = arg.substr(1);
    return rest.empty() ? base : base + parseOrNaN(rest);
}

static bool isFiniteDouble(double v) {
    return std::isfinite(v);
}

// ---- command table types ----

struct CommandDefinition {
    std::string id;
    std::string usage; // empty = not sent
    std::string description;
    int permissionLevel;
    bool isCheat;
};

using CommandCallback = std::function<std::string(Client*, const std::vector<std::string>&)>;

// ---- command definitions ----

static const std::vector<CommandDefinition> commandDefinitions = {
    { "game_set_tank",          "[tank]",                        "Changes your tank to the given class",                                                                                                                config::AccessLevel::BetaAccess,  true  },
    { "game_set_level",         "[level]",                       "Changes your level to the given integer",                                                                                                             config::AccessLevel::BetaAccess,  true  },
    { "game_set_score",         "[score]",                       "Changes your score to the given integer",                                                                                                             config::AccessLevel::BetaAccess,  true  },
    { "game_set_stat",          "[stat num] [points]",           "Set the value of the given attribute. Values can be greater than the maximum capacity. [stat num] is equivalent to the number that appears in the UI", config::AccessLevel::FullAccess,  true  },
    { "game_set_stat_max",      "[stat num] [max]",              "Sets the max value of the given attribute. [stat num] is equivalent to the number that appears in the UI",                                             config::AccessLevel::FullAccess,  true  },
    { "game_add_upgrade_points","[points]",                      "Adds upgrade points",                                                                                                                                 config::AccessLevel::FullAccess,  true  },
    { "game_teleport",          "[x] [y]",                       "Teleports you to the given position",                                                                                                                 config::AccessLevel::FullAccess,  true  },
    { "game_claim",             "[entityName]",                  "Attempts claiming an entity of the given type",                                                                                                       config::AccessLevel::BetaAccess,  false },
    { "game_invincibility",     "[?value]",                      "Toggles invincibility.",                                                                                                                              config::AccessLevel::FullAccess,  true  },
    { "game_announce",          "[message] [?color] [?time] [?id]", "Announce a message to the current game server. Example usage: 'Hello World' 0xFF0000 3000 msgId",                                                  config::AccessLevel::FullAccess,  false },
    { "game_golden_name",       "",                              "Toggles the golden nickname color that appears upon using cheats",                                                                                    config::AccessLevel::FullAccess,  false },
    { "game_neutral",           "",                              "Sets your tank's team to the neutral team",                                                                                                           config::AccessLevel::FullAccess,  false },
    { "admin_summon",           "[entityName] [?count] [?x] [?y]", "Spawns entities at the given coordinates",                                                                                                          config::AccessLevel::FullAccess,  false },
    { "admin_kill_all",         "",                              "Kills all living entities in the arena",                                                                                                              config::AccessLevel::FullAccess,  false },
    { "admin_kill_entity",      "[entityName]",                  "Kills all entities of the given type (might include self)",                                                                                           config::AccessLevel::FullAccess,  false },
    { "admin_close_arena",      "",                              "Closes the current arena",                                                                                                                            config::AccessLevel::FullAccess,  false },
};

static const CommandDefinition* findCommand(const std::string& id) {
    for (const CommandDefinition& d : commandDefinitions) if (d.id == id) return &d;
    return nullptr;
}

std::string commandsJson() {
    if (!config::enableCommands) return "[]";
    std::string j = "[";
    bool first = true;
    for (const CommandDefinition& d : commandDefinitions) {
        if (!first) j += ",";
        first = false;
        j += "{\"id\":\"" + d.id + "\",";
        if (!d.usage.empty()) j += "\"usage\":\"" + d.usage + "\",";
        j += "\"description\":\"" + d.description + "\",\"permissionLevel\":" + std::to_string(d.permissionLevel) +
             ",\"isCheat\":" + (d.isCheat ? "true" : "false") + "}";
    }
    j += "]";
    return j;
}

// ---- command callbacks ----

static std::string cmd_game_set_tank(Client* client, const std::vector<std::string>& args) {
    if (args.empty()) return "";
    TankDefinition* tankDef = getTankByName(args[0]);
    Entity* player = client->camera ? client->camera->cameraData.player : nullptr;
    if (!tankDef || !Entity::exists(player) || !TankBody::isTank(player)) return "";
    if (tankDef->flags.devOnly && client->accessLevel != config::AccessLevel::FullAccess) return "";
    static_cast<TankBody*>(player)->setTank(tankDef->id);
    return "";
}

static std::string cmd_game_set_level(Client* client, const std::vector<std::string>& args) {
    if (args.empty()) return "";
    double level = parseOrNaN(args[0]);
    Entity* player = client->camera ? client->camera->cameraData.player : nullptr;
    if (!isFiniteDouble(level) || !Entity::exists(player) || !TankBody::isTank(player)) return "";
    double finalLevel = (client->accessLevel == config::AccessLevel::FullAccess)
        ? level
        : std::min((double)config::maxPlayerLevel, level);
    client->camera->setLevel(finalLevel);
    return "";
}

static constexpr double MAX_SAFE_INTEGER =  9007199254740991.0;
static constexpr double MIN_SAFE_INTEGER = -9007199254740991.0;

static std::string cmd_game_set_score(Client* client, const std::vector<std::string>& args) {
    if (args.empty()) return "";
    double score = parseOrNaN(args[0]);
    ClientCamera* camera = client->camera;
    Entity* player = camera ? camera->cameraData.player : nullptr;
    if (!isFiniteDouble(score) || score > MAX_SAFE_INTEGER || score < MIN_SAFE_INTEGER
            || !Entity::exists(player) || !TankBody::isTank(player) || !camera) return "";
    camera->setScore(score);
    return "";
}

static std::string cmd_game_set_stat_max(Client* client, const std::vector<std::string>& args) {
    if (args.size() < 2) return "";
    double statIdRaw = parseOrNaN(args[0]);
    double statMax   = parseOrNaN(args[1]);
    if (!isFiniteDouble(statIdRaw) || !isFiniteDouble(statMax)) return ""; // before the (int) cast: (int)NaN is UB
    int statId = StatCount - (int)statIdRaw;
    CameraData* cameraData = client->camera ? &client->camera->cameraData : nullptr;
    Entity* player = cameraData ? cameraData->player : nullptr;
    if (statId < 0 || statId >= StatCount
            || !Entity::exists(player) || !TankBody::isTank(player) || !cameraData) return "";
    double clampedMax = std::max(statMax, 0.0);
    cameraData->statLimits.set(statId, clampedMax);
    double cur = cameraData->statLevels.values[statId];
    if (cur > clampedMax) cameraData->statLevels.set(statId, clampedMax);
    return "";
}

static std::string cmd_game_set_stat(Client* client, const std::vector<std::string>& args) {
    if (args.size() < 2) return "";
    double statIdRaw   = parseOrNaN(args[0]);
    double statPoints  = parseOrNaN(args[1]);
    if (!isFiniteDouble(statIdRaw) || !isFiniteDouble(statPoints)) return ""; // before the (int) cast: (int)NaN is UB
    int statId = StatCount - (int)statIdRaw;
    ClientCamera* camera = client->camera;
    Entity* player = camera ? camera->cameraData.player : nullptr;
    if (statId < 0 || statId >= StatCount
            || !Entity::exists(player) || !TankBody::isTank(player) || !camera) return "";
    camera->setStat(statId, statPoints);
    return "";
}

static std::string cmd_game_add_upgrade_points(Client* client, const std::vector<std::string>& args) {
    if (args.empty()) return "";
    double points = parseOrNaN(args[0]);
    ClientCamera* camera = client->camera;
    CameraData* cameraData = camera ? &camera->cameraData : nullptr;
    Entity* player = cameraData ? cameraData->player : nullptr;
    if (!isFiniteDouble(points) || points > MAX_SAFE_INTEGER || points < MIN_SAFE_INTEGER
            || !Entity::exists(player) || !TankBody::isTank(player) || !camera || !cameraData) return "";
    cameraData->setStatsAvailable(cameraData->statsAvailable + points);
    return "";
}

static std::string cmd_game_teleport(Client* client, const std::vector<std::string>& args) {
    if (args.size() < 2) return "";
    const std::string& xArg = args[0];
    const std::string& yArg = args[1];
    if (xArg.empty() || yArg.empty()) return "";
    Entity* player = client->camera ? client->camera->cameraData.player : nullptr;
    if (!Entity::exists(player) || !ObjectEntity::isObject(player)) return "";
    ObjectEntity* obj = static_cast<ObjectEntity*>(player);
    double x = isRelativeArg(xArg) ? resolveRelative(xArg, obj->positionData.x) : parseOrNaN(xArg);
    double y = isRelativeArg(yArg) ? resolveRelative(yArg, obj->positionData.y) : parseOrNaN(yArg);
    if (!isFiniteDouble(x) || !isFiniteDouble(y)) return "";
    obj->positionData.x = x;
    obj->positionData.y = y;
    obj->setVelocity(0, 0);
    obj->entityState |= EntityStateFlags::needsCreate | EntityStateFlags::needsDelete;
    return "";
}

// Predicate type used by game_claim.
using ClaimPredicate = std::function<bool(ObjectEntity*)>;

static const std::unordered_map<std::string, ClaimPredicate>& claimPredicates() {
    // ArenaCloser, Mothership, AutoTurret identify via arenaMobID; Dominator via its tag.
    static const std::unordered_map<std::string, ClaimPredicate> table = {
        { "ArenaCloser",  [](ObjectEntity* o) { return o->arenaMobID == "ArenaCloser"; } },
        { "Dominator",    [](ObjectEntity* o) { return (o->entityTags & EntityTags::isDominator) != 0; } },
        { "Mothership",   [](ObjectEntity* o) { return o->arenaMobID == "Mothership"; } },
        { "Shape",        [](ObjectEntity* o) { return (o->entityTags & EntityTags::isShape) != 0; } },
        { "Boss",         [](ObjectEntity* o) { return AbstractBoss::isBoss(o); } },
        { "AutoTurret",   [](ObjectEntity* o) { return o->arenaMobID == "AutoTurret"; } },
    };
    return table;
}

static std::string cmd_game_claim(Client* client, const std::vector<std::string>& args) {
    if (args.empty()) return "";
    auto& table = claimPredicates();
    auto it = table.find(args[0]);
    if (it == table.end() || !client->camera || client->camera->game->entities->AIs.empty()) return "";
    const ClaimPredicate& predicate = it->second;
    std::vector<AI*> ais = client->camera->game->entities->AIs;
    for (AI* ai : ais) {
        if (ai->state == AIState::possessed) continue;
        if (!ai->owner || !predicate(ai->owner)) continue;
        if (client->possess(ai)) client->achievementsBlocked = true;
        return "";
    }
    return "";
}

static std::string cmd_game_invincibility(Client* client, const std::vector<std::string>& args) {
    Entity* player = client->camera ? client->camera->cameraData.player : nullptr;
    if (!Entity::exists(player) || !TankBody::isTank(player)) return "";
    TankBody* tank = static_cast<TankBody*>(player);
    const std::string& activeArg = args.empty() ? "" : args[0];
    if (activeArg == "on") tank->setInvulnerability(true);
    else if (activeArg == "off") tank->setInvulnerability(false);
    else tank->setInvulnerability(!tank->isInvulnerable);
    return std::string("Invincibility: ") + (tank->isInvulnerable ? "ON" : "OFF");
}

static std::string cmd_game_announce(Client* client, const std::vector<std::string>& args) {
    const std::string message = args.size() > 0 ? args[0] : "";
    const std::string color   = args.size() > 1 ? args[1] : "0x000000";
    const std::string time    = args.size() > 2 ? args[2] : "15000";
    const std::string id      = args.size() > 3 ? args[3] : "";
    GameServer* game = client->camera ? client->camera->game : nullptr;
    if (!game) return "";
    uint32_t colorVal = 0;
    try { colorVal = (uint32_t)std::stoul(color, nullptr, 0); } catch (...) {}
    double timeVal = parseOrNaN(time);
    game->broadcast()
        .u8(ClientBound::Notification)
        .stringNT(message)
        .u32(colorVal)
        .floatLE((float)timeVal)
        .stringNT(id).send();
    return "";
}

static std::string cmd_game_golden_name(Client* client, const std::vector<std::string>& args) {
    client->setHasCheated(!client->hasCheated());
    return "";
}

static std::string cmd_game_neutral(Client* client, const std::vector<std::string>& args) {
    Entity* team = client->camera ? client->camera->game->arena : nullptr;
    Entity* player = client->camera ? client->camera->cameraData.player : nullptr;
    if (!team || !player) return "";
    if (!ObjectEntity::isObject(player)) return "";
    TeamEntity::setTeam(team, static_cast<ObjectEntity*>(player));
    return "";
}

// Factory table for admin_summon.
using EntityFactory = std::function<ObjectEntity*(GameServer*)>;

static const std::unordered_map<std::string, EntityFactory>& summonFactories() {
    static const std::unordered_map<std::string, EntityFactory> table = {
        { "Defender",         [](GameServer* g) -> ObjectEntity* { return new Defender(g); } },
        { "Summoner",         [](GameServer* g) -> ObjectEntity* { return new Summoner(g); } },
        { "Guardian",         [](GameServer* g) -> ObjectEntity* { return new Guardian(g); } },
        { "FallenOverlord",   [](GameServer* g) -> ObjectEntity* { return new FallenOverlord(g); } },
        { "FallenBooster",    [](GameServer* g) -> ObjectEntity* { return new FallenBooster(g); } },
        { "FallenAC",         [](GameServer* g) -> ObjectEntity* { return new FallenAC(g); } },
        { "FallenSpike",      [](GameServer* g) -> ObjectEntity* { return new FallenSpike(g); } },
        { "FallenMegaTrapper",[](GameServer* g) -> ObjectEntity* { return new FallenMegaTrapper(g); } },
        { "ArenaCloser",      [](GameServer* g) -> ObjectEntity* { return new ArenaCloser(g); } },
        { "Mothership",       [](GameServer* g) -> ObjectEntity* { return new Mothership(g); } },
        { "Crasher",          [](GameServer* g) -> ObjectEntity* { return new Crasher(g); } },
        { "Pentagon",         [](GameServer* g) -> ObjectEntity* { return new Pentagon(g); } },
        { "Square",           [](GameServer* g) -> ObjectEntity* { return new Square(g); } },
        { "Triangle",         [](GameServer* g) -> ObjectEntity* { return new Triangle(g); } },
    };
    return table;
}

static std::string cmd_admin_summon(Client* client, const std::vector<std::string>& args) {
    if (args.empty()) return "";
    const std::string& entityArg = args[0];
    double count = args.size() > 1 ? parseOrNaN(args[1]) : 1.0;
    if (!isFiniteDouble(count) || count < 0) return "";

    Entity* playerEnt = client->camera ? client->camera->cameraData.player : nullptr;
    ObjectEntity* player = (Entity::exists(playerEnt) && ObjectEntity::isObject(playerEnt))
        ? static_cast<ObjectEntity*>(playerEnt) : nullptr;

    double x = args.size() > 2 ? parseOrNaN(args[2]) : 0.0;
    double y = args.size() > 3 ? parseOrNaN(args[3]) : 0.0;
    if (player) {
        if (args.size() > 2 && isRelativeArg(args[2])) x = resolveRelative(args[2], player->positionData.x);
        if (args.size() > 3 && isRelativeArg(args[3])) y = resolveRelative(args[3], player->positionData.y);
    }

    GameServer* game = client->camera ? client->camera->game : nullptr;
    auto& table = summonFactories();
    auto it = table.find(entityArg);
    if (!game || it == table.end()) return "";

    bool hasPos = isFiniteDouble(x) && isFiniteDouble(y);
    int n = (int)std::min(count, 1000.0); // clamp: avoid exhausting the entity table in one tick
    for (int i = 0; i < n; ++i) {
        ObjectEntity* entity = it->second(game);
        if (hasPos) {
            entity->positionData.x = x;
            entity->positionData.y = y;
        }
    }
    return "";
}

static std::string cmd_admin_kill_all(Client* client, const std::vector<std::string>& args) {
    GameServer* game = client->camera ? client->camera->game : nullptr;
    if (!game) return "";
    Entity* self = client->camera ? client->camera->cameraData.player : nullptr;
    for (int id = 0; id <= game->entities->lastId; ++id) {
        Entity* entity = game->entities->inner[id];
        if (!Entity::exists(entity)) continue;
        if (!LivingEntity::isLive(entity)) continue;
        if (entity == self) continue;
        PhysicsData* pd = entity->getPhysicsData();
        if (pd && (pd->flags & PhysicsFlags::showsOnMap)) continue;
        static_cast<LivingEntity*>(entity)->destroy();
    }
    return "";
}

// Predicate type for admin_kill_entity.
using KillPredicate = std::function<bool(Entity*)>;

static const std::unordered_map<std::string, KillPredicate>& killPredicates() {
    // arenaMobID stands in for instanceof here.
    static const std::unordered_map<std::string, KillPredicate> table = {
        { "ArenaCloser", [](Entity* e) { return ObjectEntity::isObject(e) && static_cast<ObjectEntity*>(e)->arenaMobID == "ArenaCloser"; } },
        { "Dominator",   [](Entity* e) { return ObjectEntity::isObject(e) && (static_cast<ObjectEntity*>(e)->entityTags & EntityTags::isDominator) != 0; } },
        { "Bullet",      [](Entity* e) { return ObjectEntity::isObject(e) && (static_cast<ObjectEntity*>(e)->entityTags & EntityTags::isBullet) != 0; } },
        { "Tank",        [](Entity* e) { return TankBody::isTank(e); } },
        { "Shape",       [](Entity* e) { return ObjectEntity::isObject(e) && (static_cast<ObjectEntity*>(e)->entityTags & EntityTags::isShape) != 0; } },
        { "Boss",        [](Entity* e) { return AbstractBoss::isBoss(e); } },
    };
    return table;
}

static std::string cmd_admin_kill_entity(Client* client, const std::vector<std::string>& args) {
    if (args.empty()) return "";
    GameServer* game = client->camera ? client->camera->game : nullptr;
    auto& table = killPredicates();
    auto it = table.find(args[0]);
    if (!game || it == table.end()) return "";
    const KillPredicate& pred = it->second;
    for (int id = 0; id <= game->entities->lastId; ++id) {
        Entity* entity = game->entities->inner[id];
        if (Entity::exists(entity) && pred(entity)) static_cast<ObjectEntity*>(entity)->destroy();
    }
    return "";
}

static std::string cmd_admin_close_arena(Client* client, const std::vector<std::string>& args) {
    if (client->camera) client->camera->game->arena->close();
    return "";
}

// ---- callback dispatch table ----

static const std::unordered_map<std::string, CommandCallback>& commandCallbacks() {
    static const std::unordered_map<std::string, CommandCallback> table = {
        { "game_set_tank",           cmd_game_set_tank           },
        { "game_set_level",          cmd_game_set_level          },
        { "game_set_score",          cmd_game_set_score          },
        { "game_set_stat",           cmd_game_set_stat           },
        { "game_set_stat_max",       cmd_game_set_stat_max       },
        { "game_add_upgrade_points", cmd_game_add_upgrade_points },
        { "game_teleport",           cmd_game_teleport           },
        { "game_claim",              cmd_game_claim              },
        { "game_invincibility",      cmd_game_invincibility      },
        { "game_announce",           cmd_game_announce           },
        { "game_golden_name",        cmd_game_golden_name        },
        { "game_neutral",            cmd_game_neutral            },
        { "admin_summon",            cmd_admin_summon            },
        { "admin_kill_all",          cmd_admin_kill_all          },
        { "admin_kill_entity",       cmd_admin_kill_entity       },
        { "admin_close_arena",       cmd_admin_close_arena       },
    };
    return table;
}

// ---- public entry point ----

void executeCommand(Client* client, const std::string& cmd, const std::vector<std::string>& args) {
    const CommandDefinition* def = findCommand(cmd);
    auto cbIt = commandCallbacks().find(cmd);
    if (!def || cbIt == commandCallbacks().end()) {
        util::saveToVLog(client->toString() + " tried to run the invalid command " + cmd);
        return;
    }
    if (client->accessLevel < def->permissionLevel) {
        util::saveToVLog(client->toString() + " tried to run the command " + cmd + " with a permission that was too low");
        return;
    }
    if (def->isCheat) client->setHasCheated(true);
    std::string response = cbIt->second(client, args);
    if (!response.empty()) {
        client->notify(response, 0x00ffa0, 5000, "cmd-callback" + def->id);
    }
}
