// A busy scene, for sizing the entity slots: how many objects does one tank have on screen?
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <cmath>
#include <memory>
#include <vector>

#include "Const/Enums.h"
#include "Entity/Live.h"
#include "Entity/Shape/Pentagon.h"
#include "Entity/Shape/Square.h"
#include "Entity/Shape/Triangle.h"
#include "Entity/Tank/Projectile/Drone.h"
#include "Entity/Tank/TankBody.h"
#include "Game.h"
#include "Misc/BossManager.h"
#include "Misc/ShapeManager.h"
#include "Native/Arena.h"
#include "Native/Camera.h"
#include "Native/Manager.h"
#include "Util.h"

namespace py = pybind11;

namespace bots {

namespace {

class NoShapes : public ShapeManager {
public:
    explicit NoShapes(ArenaEntity* arena) : ShapeManager(arena) {}

protected:
    int wantedShapes() const override { return 0; }
};

class OpenArena : public ArenaEntity {
public:
    OpenArena(GameServer* game, double size) : ArenaEntity(game) {
        shapes = std::make_unique<NoShapes>(this);
        bossManager.reset();
        updateBounds(size, size);
        state = ArenaState::OPEN;
    }
};

struct Actor {
    TankBody* tank;
    std::shared_ptr<Inputs> inputs;
    double aimX, aimY;
    bool fire;
    double x, y;
};

}

// tanks: dicts of tank (class id), x, y, fire, aim_x, aim_y. The first is the one whose screen is
// counted; it never fires. Tanks can't be hurt or move, so the scene stays busy, and shapes destroyed are replaced within shape_radius of the first tank.
// Returns per tick: [tanks, bullets, drones and minions, shapes] on the first tank's screen.
static py::list crowd(py::list tanks, int shapes, double shapeRadius, int ticks, double arena, uint64_t seed) {
    util::seedRandom(seed);
    GameServer* game = new GameServer([arena](GameServer* g) { return (ArenaEntity*)new OpenArena(g, arena); },
                                      "crowd", "Crowd");
    int stats[StatCount] = {5, 7, 6, 6, 5, 0, 4, 0};
    std::vector<Actor> actors;
    for (auto item : tanks) {
        py::dict d = item.cast<py::dict>();
        CameraEntity* cam = new CameraEntity(game);
        auto inputs = std::make_shared<Inputs>();
        TankBody* t = new TankBody(game, cam, inputs);
        cam->cameraData.setPlayer(t);
        int id = d["tank"].cast<int>();
        if (id != Tank::Basic) t->setTank(id);
        cam->setLevel(45);
        for (int i = 0; i < StatCount; ++i) cam->setStat(i, std::min((double)stats[i], cam->cameraData.statLimits.values[i]));
        cam->cameraData.setStatsAvailable(0);
        t->styleData.setFlags(t->styleData.flags & ~StyleFlags::isFlashing);
        // Unkillable, so every gun keeps firing; bullets still wear out on them.
        t->damageReduction = 0;
        t->positionData.setX(d["x"].cast<double>());
        t->positionData.setY(d["y"].cast<double>());
        actors.push_back({t, inputs, d["aim_x"].cast<double>(), d["aim_y"].cast<double>(), d["fire"].cast<bool>(),
                          t->positionData.x, t->positionData.y});
    }
    TankBody* viewer = actors[0].tank;
    actors[0].fire = false;

    std::vector<Ref<ObjectEntity>> live;
    auto addShape = [&]() {
        double r = shapeRadius * std::sqrt(util::randomFloat()), a = util::randomFloat() * util::PI2;
        double pick = util::randomFloat();
        ObjectEntity* s = pick < 0.5 ? (ObjectEntity*)new Square(game) : pick < 0.8 ? (ObjectEntity*)new Triangle(game)
                                                                                    : (ObjectEntity*)new Pentagon(game);
        s->positionData.setX(viewer->positionData.x + std::cos(a) * r);
        s->positionData.setY(viewer->positionData.y + std::sin(a) * r);
        live.push_back(s);
    };
    for (int i = 0; i < shapes; ++i) addShape();

    py::list out;
    for (int tick = 0; tick < ticks; ++tick) {
        for (Actor& a : actors) {
            a.inputs->movement.x = a.inputs->movement.y = 0;
            a.inputs->mouse.x = a.aimX;
            a.inputs->mouse.y = a.aimY;
            a.inputs->flags = a.fire ? InputFlags::leftclick : 0;
            // Pinned in place, or recoil would push the shooters away over time.
            a.tank->positionData.setX(a.x);
            a.tank->positionData.setY(a.y);
            a.tank->velocity.x = a.tank->velocity.y = 0;
        }
        game->tickLoop();
        std::vector<Ref<ObjectEntity>> kept;
        for (auto& s : live) if (Entity::exists(s.get()) && !s->deletionAnimation) kept.push_back(s);
        live.swap(kept);
        while ((int)live.size() < shapes) addShape();

        double fov = viewer->cameraEntity->cameraData.FOV;
        double hw = 960 / fov, hh = 540 / fov;
        int counts[4] = {0, 0, 0, 0};
        EntityManager* em = game->entities.get();
        for (int id = 0; id <= em->lastId; ++id) {
            Entity* e = em->inner[id];
            if (!Entity::exists(e) || !LivingEntity::isLive(e) || e == (Entity*)viewer) continue;
            ObjectEntity* o = (ObjectEntity*)e;
            if (o->deletionAnimation) continue;
            double r = o->physicsData.size;
            if (std::abs(o->positionData.x - viewer->positionData.x) > hw + r) continue;
            if (std::abs(o->positionData.y - viewer->positionData.y) > hh + r) continue;
            int kind = (o->entityTags & EntityTags::isTank) ? 0
                       : dynamic_cast<Drone*>(o) ? 2
                       : (o->entityTags & EntityTags::isBullet) ? 1 : 3;
            counts[kind] += 1;
        }
        out.append(py::make_tuple(counts[0], counts[1], counts[2], counts[3]));
    }
    live.clear();
    delete game;
    return out;
}

void bindCrowd(py::module_& mod) {
    mod.def("crowd", &crowd, py::arg("tanks"), py::arg("shapes") = 10, py::arg("shape_radius") = 1500,
            py::arg("ticks") = 900, py::arg("arena") = 9000, py::arg("seed") = 1);
}

}
