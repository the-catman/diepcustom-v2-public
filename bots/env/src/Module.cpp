#include <pybind11/numpy.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <climits>
#include <stdexcept>
#include <unistd.h>

#include "Config.h"
#include "Const/TankDefinitions.h"
#include "Entity/Live.h"
#include "Entity/Tank/Barrel.h"
#include "Entity/Tank/Projectile/Drone.h"
#include "Entity/Tank/TankBody.h"
#include "Const/Enums.h"
#include "Game.h"
#include "Native/Manager.h"
#include "Util.h"
#include "Vec.h"

namespace py = pybind11;
using namespace bots;

// A per-slot setting as a list, one entry per tank in the game.
template <typename T>
static std::vector<T> perSlot(const Config& c, const T* values) {
    return std::vector<T>(values, values + c.players);
}

// Sets the first slots' entries of a per-slot setting.
template <typename T, typename V>
static void setSlots(T* values, const std::vector<V>& in, const char* what) {
    if (in.empty() || in.size() > MAX_PLAYERS) {
        throw std::runtime_error(std::string(what) + " needs 1 to " + std::to_string(MAX_PLAYERS) + " entries");
    }
    std::copy(in.begin(), in.end(), values);
}

// The tank definitions load from the server's data folder relative to the working directory.
static void init(const std::string& serverDir) {
    static bool done = false;
    if (done) return;
    char cwd[PATH_MAX];
    if (!getcwd(cwd, sizeof(cwd))) throw std::runtime_error("getcwd failed");
    if (chdir(serverDir.c_str()) != 0) throw std::runtime_error("can't enter " + serverDir);
    config::init();
    initTankDefinitions();
    if (chdir(cwd) != 0) throw std::runtime_error("can't return to " + std::string(cwd));
    done = true;
}

// Entity arrays are only as wide as the busiest row needs. seen is how many entities each row had on
// screen, which is more than it got when entityCap cut some off.
static py::dict observe(Vec& v) {
    py::ssize_t n = v.rows();
    py::array_t<float> self({n, (py::ssize_t)SELF_DIM});
    float* sp = self.mutable_data();
    int width;
    {
        py::gil_scoped_release release;
        width = v.observe(sp);
    }
    py::array_t<float> ents({n, (py::ssize_t)width, (py::ssize_t)ENT_DIM});
    py::array_t<bool> mask({n, (py::ssize_t)width});
    py::array_t<bool> targetable({n, (py::ssize_t)width});
    {
        py::gil_scoped_release release;
        v.copyEntities(width, ents.mutable_data(), (uint8_t*)mask.mutable_data(), (uint8_t*)targetable.mutable_data());
    }
    py::array_t<int32_t> seen(n), tank(n), enemyTank(n);
    py::array_t<bool> alive(n);
    for (py::ssize_t i = 0; i < n; ++i) {
        seen.mutable_at(i) = v.seen[i];
        tank.mutable_at(i) = v.tankOf[i];
        enemyTank.mutable_at(i) = v.enemyOf[i];
        alive.mutable_at(i) = v.aliveOf[i];
    }
    py::dict d;
    d["self"] = self;
    d["ents"] = ents;
    d["mask"] = mask;
    d["targetable"] = targetable;
    d["seen"] = seen;
    d["tank"] = tank;
    d["enemy_tank"] = enemyTank;
    d["alive"] = alive;
    return d;
}

// repel and reach are optional: without them nobody right clicks and every mouse is far away.
static py::dict step(Vec& v, py::array_t<int32_t> move, py::array_t<bool> fire, py::array_t<int32_t> target,
                     py::array_t<float> aim, std::optional<py::array_t<bool>> repel,
                     std::optional<py::array_t<float>> reach) {
    int n = v.rows();
    auto m = move.unchecked<1>();
    auto f = fire.unchecked<1>();
    auto t = target.unchecked<1>();
    auto a = aim.unchecked<1>();
    bool bad = m.shape(0) != n || f.shape(0) != n || t.shape(0) != n || a.shape(0) != n
               || (repel && repel->shape(0) != n) || (reach && reach->shape(0) != n);
    if (bad) throw std::runtime_error("step: expected " + std::to_string(n) + " actions");
    std::vector<Action> actions(n);
    for (int i = 0; i < n; ++i) {
        actions[i] = {m(i), f(i), t(i), a(i)};
        if (repel) actions[i].repel = repel->at(i);
        if (reach) actions[i].reach = reach->at(i);
    }
    std::vector<Outcome> outs(n);
    std::vector<Credit> credits;
    {
        py::gil_scoped_release release;
        v.step(actions.data(), outs.data(), credits);
    }

    py::array_t<float> reward(n), dealt(n), taken(n), shapeTaken(n);
    py::array_t<bool> done(n), won(n), lost(n), timedOut(n), diedToShape(n), apart(n), gameOver(n);
    py::array_t<bool> teamWon(n), teamLost(n), eliminated(n), targeted(n), sameTarget(n);
    py::array_t<int32_t> shots(n), hits(n), firstSeen(n), shapeBlocked(n), kills(n), stolen(n);
    py::array_t<int32_t> teamLeft(n), handoff(n), mateKills(n), assists(n);
    for (int i = 0; i < n; ++i) {
        const Outcome& o = outs[i];
        reward.mutable_at(i) = o.reward;
        dealt.mutable_at(i) = o.dealt;
        taken.mutable_at(i) = o.taken;
        done.mutable_at(i) = o.done;
        won.mutable_at(i) = o.won;
        lost.mutable_at(i) = o.lost;
        timedOut.mutable_at(i) = o.timedOut;
        shots.mutable_at(i) = o.shots;
        hits.mutable_at(i) = o.hits;
        firstSeen.mutable_at(i) = o.firstSeen;
        shapeTaken.mutable_at(i) = o.shapeTaken;
        shapeBlocked.mutable_at(i) = o.shapeBlocked;
        diedToShape.mutable_at(i) = o.diedToShape;
        kills.mutable_at(i) = o.kills;
        stolen.mutable_at(i) = o.stolen;
        apart.mutable_at(i) = o.apart;
        gameOver.mutable_at(i) = o.gameOver;
        teamWon.mutable_at(i) = o.teamWon;
        teamLost.mutable_at(i) = o.teamLost;
        teamLeft.mutable_at(i) = o.teamLeft;
        eliminated.mutable_at(i) = o.eliminated;
        handoff.mutable_at(i) = o.handoff;
        mateKills.mutable_at(i) = o.mateKills;
        assists.mutable_at(i) = o.assists;
        targeted.mutable_at(i) = o.targeted;
        sameTarget.mutable_at(i) = o.sameTarget;
    }
    py::ssize_t c = (py::ssize_t)credits.size();
    py::array_t<int32_t> creditRow(c), creditAgo(c);
    py::array_t<float> creditValue(c);
    for (py::ssize_t i = 0; i < c; ++i) {
        creditRow.mutable_at(i) = credits[i].slot;
        creditAgo.mutable_at(i) = credits[i].stepsAgo;
        creditValue.mutable_at(i) = credits[i].value;
    }

    py::dict d = observe(v);
    d["reward"] = reward;
    d["done"] = done;
    d["won"] = won;
    d["lost"] = lost;
    d["timed_out"] = timedOut;
    d["dealt"] = dealt;
    d["taken"] = taken;
    d["shots"] = shots;
    d["hits"] = hits;
    d["first_seen"] = firstSeen;
    d["shape_taken"] = shapeTaken;
    d["shape_blocked"] = shapeBlocked;
    d["died_to_shape"] = diedToShape;
    d["kills"] = kills;
    d["stolen"] = stolen;
    d["apart"] = apart;
    d["game_over"] = gameOver;
    d["team_won"] = teamWon;
    d["team_lost"] = teamLost;
    d["team_left"] = teamLeft;
    d["eliminated"] = eliminated;
    // The slot of the teammate a tank that died this decision hands its future to, or -1.
    d["handoff"] = handoff;
    d["mate_kills"] = mateKills;
    d["assists"] = assists;
    d["targeted"] = targeted;
    d["same_target"] = sameTarget;
    d["credit_row"] = creditRow;
    d["credit_ago"] = creditAgo;
    d["credit_value"] = creditValue;
    return d;
}

static py::dict duelState(Vec& v, int d) {
    if (d < 0 || d >= (int)v.duels.size()) throw std::out_of_range("duel index");
    Duel& duel = *v.duels[d];
    py::dict out;
    out["tick"] = duel.game->tick;
    out["decision"] = duel.decision;
    py::list tanks;
    for (int s = 0; s < duel.config.players; ++s) {
        TankBody* t = duel.tank(s);
        py::dict info;
        if (t) {
            info["tank"] = duel.slots[s].tankId;
            info["x"] = t->positionData.x;
            info["y"] = t->positionData.y;
            info["vx"] = t->velocity.x;
            info["vy"] = t->velocity.y;
            info["angle"] = t->positionData.angle;
            info["health"] = t->healthData.health;
            info["max_health"] = t->healthData.maxHealth;
            info["size"] = t->physicsData.size;
            info["fov"] = t->cameraEntity ? t->cameraEntity->cameraData.FOV : 0.0;
            info["reach"] = duel.slots[s].ballistics.reach;
            const Inputs& in = *duel.slots[s].inputs;
            info["mouse_x"] = in.mouse.x;
            info["mouse_y"] = in.mouse.y;
            info["attract"] = (in.flags & InputFlags::leftclick) != 0;
            info["repel"] = (in.flags & InputFlags::rightclick) != 0;
        }
        tanks.append(info);
    }
    out["tanks"] = tanks;
    int entities = 0;
    EntityManager* em = duel.game->entities.get();
    for (int id = 0; id <= em->lastId; ++id) entities += Entity::exists(em->inner[id]) ? 1 : 0;
    out["entities"] = entities;
    out["shapes"] = duel.shapeCount();
    return out;
}

// Every live object in a duel: x, y, radius, kind (0 tank, 1 bullet, 2 drone, or a shape's sides: 3 triangle,
// 4 square, 5 pentagon), side (0 or 1, 2 for shapes), angle, health fraction, id.
static py::array_t<float> duelEntities(Vec& v, int d) {
    if (d < 0 || d >= (int)v.duels.size()) throw std::out_of_range("duel index");
    Duel& duel = *v.duels[d];
    EntityManager* em = duel.game->entities.get();
    TankBody* t0 = duel.tank(0);
    std::vector<float> rows;
    for (int id = 0; id <= em->lastId; ++id) {
        Entity* e = em->inner[id];
        if (!Entity::exists(e) || !LivingEntity::isLive(e)) continue;
        ObjectEntity* o = (ObjectEntity*)e;
        bool side0 = t0 && (o == (ObjectEntity*)t0 || o->relationsData.owner.get() == (Entity*)t0);
        bool shape = o->entityTags & EntityTags::isShape;
        HealthData* hd = o->getHealthData();
        float kind = shape ? (float)o->physicsData.sides
                     : dynamic_cast<Drone*>(o) ? 2.0f
                     : (o->entityTags & EntityTags::isBullet) ? 1.0f : 0.0f;
        rows.insert(rows.end(), {(float)o->positionData.x, (float)o->positionData.y, (float)o->physicsData.size, kind,
                                 shape ? 2.0f : side0 ? 0.0f : 1.0f,
                                 (float)o->positionData.angle,
                                 (float)(hd->health / std::max(hd->maxHealth, 1.0)), (float)id});
    }
    py::array_t<float> out({(py::ssize_t)(rows.size() / 8), (py::ssize_t)8});
    std::copy(rows.begin(), rows.end(), out.mutable_data());
    return out;
}

// A tank's barrels in its own frame, facing +x: x, y, angle, length, width, is trapezoid.
static py::list duelBarrels(Vec& v, int d, int slot) {
    if (d < 0 || d >= (int)v.duels.size()) throw std::out_of_range("duel index");
    if (slot < 0 || slot >= v.duels[d]->config.players) throw std::out_of_range("slot");
    py::list out;
    TankBody* t = v.duels[d]->tank(slot);
    if (!t) return out;
    for (Barrel* b : t->barrels) {
        py::list row;
        row.append(b->positionData.x);
        row.append(b->positionData.y);
        row.append(b->positionData.angle);
        row.append(b->physicsData.size);
        row.append(b->physicsData.width);
        row.append((b->physicsData.flags & PhysicsFlags::isTrapezoid) != 0);
        out.append(row);
    }
    return out;
}

namespace bots {
void bindCrowd(py::module_& mod);
}

PYBIND11_MODULE(diepenv, mod) {
    mod.def("init", &init);
    bindCrowd(mod);
    mod.attr("SELF_DIM") = SELF_DIM;
    mod.attr("ENT_DIM") = ENT_DIM;
    mod.attr("OLD_SELF_DIM") = OLD_SELF_DIM;
    mod.attr("OLD_ENT_DIM") = OLD_ENT_DIM;
    mod.attr("CLASS_DIM") = CLASS_DIM;
    mod.attr("CLASS_ENT_DIM") = CLASS_ENT_DIM;
    mod.attr("KIND_DIM") = KIND_DIM;
    mod.attr("ENEMIES_LEFT") = ENEMIES_LEFT;
    mod.attr("MATES") = MATES;
    mod.attr("MATES_LEFT") = MATES_LEFT;
    mod.attr("ALLY") = ALLY;
    mod.attr("REMEMBERED") = REMEMBERED;
    mod.attr("SINCE_SEEN") = SINCE_SEEN;
    mod.attr("MAX_PLAYERS") = MAX_PLAYERS;
    mod.attr("ENT_MAX") = ENT_MAX;
    mod.attr("MOVES") = MOVES;
    mod.attr("EXTERNAL") = (int)External;
    mod.attr("STILL") = (int)Still;
    mod.attr("LINEAR") = (int)Linear;
    mod.attr("STRAFE") = (int)Strafe;
    mod.attr("FIGHTER") = (int)Fighter;

    py::class_<Config>(mod, "Config")
        .def(py::init<>())
        .def_readwrite("players", &Config::players)
        .def_readwrite("arena_size", &Config::arenaSize)
        .def_readwrite("max_decisions", &Config::maxDecisions)
        .def_readwrite("repeat", &Config::repeat)
        .def_readwrite("level", &Config::level)
        .def_readwrite("spawn_min", &Config::spawnMin)
        .def_readwrite("spawn_max", &Config::spawnMax)
        .def_readwrite("mirror_spawn", &Config::mirrorSpawn)
        .def_readwrite("win_reward", &Config::winReward)
        .def_readwrite("loss_reward", &Config::lossReward)
        .def_readwrite("kill_reward", &Config::killReward)
        .def_readwrite("eliminated_reward", &Config::eliminatedReward)
        .def_readwrite("mate_min", &Config::mateMin)
        .def_readwrite("mate_max", &Config::mateMax)
        .def_readwrite("dealt_scale", &Config::dealtScale)
        .def_readwrite("taken_scale", &Config::takenScale)
        .def_readwrite("credit_shots", &Config::creditShots)
        .def_readwrite("aim_noise", &Config::aimNoise)
        .def_readwrite("fighter_shoots", &Config::fighterShoots)
        .def_readwrite("fighter_dodges", &Config::fighterDodges)
        .def_readwrite("expose_scripted", &Config::exposeScripted)
        .def_readwrite("symmetry", &Config::symmetry)
        .def_readwrite("stock_drones", &Config::stockDrones)
        .def_readwrite("entity_cap", &Config::entityCap)
        .def_readwrite("shapes_min", &Config::shapesMin)
        .def_readwrite("shapes_max", &Config::shapesMax)
        .def_readwrite("shapes_empty", &Config::shapesEmpty)
        .def_readwrite("pentagon_rich", &Config::pentagonRich)
        // Per slot, a list of (class, 8 stat points in server order, weight); empty plays tanks and stats.
        .def_property(
            "choices",
            [](const Config& c) {
                std::vector<std::vector<std::tuple<int, std::vector<int>, double>>> out(c.players);
                for (int slot = 0; slot < c.players; ++slot)
                    for (const Choice& ch : c.choices[slot])
                        out[slot].push_back({ch.tank, std::vector<int>(ch.stats, ch.stats + 8), ch.weight});
                return out;
            },
            [](Config& c, const std::vector<std::vector<std::tuple<int, std::vector<int>, double>>>& in) {
                if (in.size() > MAX_PLAYERS) throw std::runtime_error("choices needs a list per slot");
                for (int slot = 0; slot < MAX_PLAYERS; ++slot) {
                    c.choices[slot].clear();
                    if (slot >= (int)in.size()) continue;
                    for (const auto& [tank, stats, weight] : in[slot]) {
                        if (stats.size() != 8) throw std::runtime_error("a choice's stats need 8 entries");
                        Choice ch;
                        ch.tank = tank;
                        std::copy(stats.begin(), stats.end(), ch.stats);
                        ch.weight = weight;
                        c.choices[slot].push_back(ch);
                    }
                }
            })
        .def_property("kinds", [](const Config& c) { return perSlot(c, c.kinds); },
                      [](Config& c, const std::vector<int>& k) { setSlots(c.kinds, k, "kinds"); })
        .def_property("tanks", [](const Config& c) { return perSlot(c, c.tanks); },
                      [](Config& c, const std::vector<int>& t) { setSlots(c.tanks, t, "tanks"); })
        .def_property("teams", [](const Config& c) { return perSlot(c, c.teams); },
                      [](Config& c, const std::vector<int>& t) { setSlots(c.teams, t, "teams"); })
        .def_property("remember", [](const Config& c) { return perSlot(c, c.remember); },
                      [](Config& c, const std::vector<bool>& r) { setSlots(c.remember, r, "remember"); })
        .def_property("learner", [](const Config& c) { return perSlot(c, c.learner); },
                      [](Config& c, const std::vector<bool>& l) { setSlots(c.learner, l, "learner"); })
        .def_property(
            "stats",
            [](const Config& c) {
                std::vector<std::vector<int>> out;
                for (int slot = 0; slot < c.players; ++slot) out.push_back({c.stats[slot], c.stats[slot] + 8});
                return out;
            },
            [](Config& c, const py::sequence& s) {
                // 8 points for every slot, or one list of 8 per slot.
                std::vector<std::vector<int>> per;
                if (s.size() == 8 && !py::isinstance<py::sequence>(s[0])) {
                    per.assign(MAX_PLAYERS, s.cast<std::vector<int>>());
                } else {
                    per = s.cast<std::vector<std::vector<int>>>();
                }
                bool ok = !per.empty() && per.size() <= MAX_PLAYERS;
                for (const auto& p : per) ok &= p.size() == 8;
                if (!ok) throw std::runtime_error("stats needs 8 entries, or a list of 8 per slot");
                for (size_t slot = 0; slot < per.size(); ++slot)
                    for (int i = 0; i < 8; ++i) c.stats[slot][i] = per[slot][i];
            });

    py::class_<Vec>(mod, "Vec")
        .def(py::init<const Config&, int, int>(), py::arg("config"), py::arg("duels"), py::arg("threads") = 1,
             py::call_guard<py::gil_scoped_release>())
        .def(py::init<const std::vector<Config>&, int>(), py::arg("configs"), py::arg("threads") = 1,
             py::call_guard<py::gil_scoped_release>())
        .def_property_readonly("rows", &Vec::rows)
        .def_property_readonly("row_map", [](const Vec& v) { return v.rowMap; })
        .def("set_config", &Vec::setConfig)
        .def("set_duel_config", &Vec::setDuelConfig)
        .def_property_readonly("duels", [](const Vec& v) { return (int)v.duels.size(); })
        .def("reset_all", &Vec::resetAll, py::call_guard<py::gil_scoped_release>())
        .def("seed", &Vec::seed)
        .def("set_frame", &Vec::setFrame)
        .def("set_paused", [](Vec& v, py::array_t<bool> paused) {
            if (paused.ndim() != 1 || paused.shape(0) != (py::ssize_t)v.duels.size())
                throw std::runtime_error("set_paused: expected one flag per duel");
            auto p = paused.unchecked<1>();
            for (py::ssize_t d = 0; d < p.shape(0); ++d) v.paused[(size_t)d] = p(d);
        })
        .def("observe", &observe)
        .def("step", &step, py::arg("move"), py::arg("fire"), py::arg("target"), py::arg("aim"),
             py::arg("repel") = py::none(), py::arg("reach") = py::none())
        .def("duel_state", &duelState)
        .def("duel_entities", &duelEntities)
        .def("duel_barrels", &duelBarrels)
        .def("duel_place_shape", [](Vec& v, int d, int kind, double x, double y) {
            if (d < 0 || d >= (int)v.duels.size()) throw std::out_of_range("duel index");
            if (kind < 3 || kind > 6) throw std::out_of_range("kind is 3 triangle, 4 square, 5 pentagon or 6 alpha");
            v.duels[d]->placeShape(kind, x, y);
        })
        // views: (follow, fov) per camera; follow is the slot whose own view it shows, or -1 for a
        // camera fixed over the arena's centre at that fov.
        .def("duel_start_replay", [](Vec& v, int d, const std::vector<std::pair<int, double>>& views,
                                     const std::vector<std::string>& names) {
            if (d < 0 || d >= (int)v.duels.size()) throw std::out_of_range("duel index");
            std::vector<ReplayView> vs;
            for (auto [follow, fov] : views) {
                if (follow < -1 || follow >= v.duels[d]->config.players) throw std::out_of_range("follow is a slot or -1");
                vs.push_back({follow, fov});
            }
            v.duels[d]->startReplay(vs, names);
        })
        // What the server sent each view since the last call: (game, view, tick, bytes).
        .def("duel_take_packets", [](Vec& v, int d) {
            if (d < 0 || d >= (int)v.duels.size()) throw std::out_of_range("duel index");
            py::list out;
            for (Packet& p : v.duels[d]->takePackets()) out.append(py::make_tuple(p.game, p.view, p.tick, py::bytes(p.bytes)));
            return out;
        });

    // The server's palette, as its /api/colors serves it.
    mod.def("colors", [] {
        py::dict out;
        for (int c = 0; c <= Color::kMaxColors; ++c) out[py::str(std::to_string(c))] = colorHex(c);
        return out;
    });
}
