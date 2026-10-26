#include "sim/Simulation.h"

#include "Const.h"

#include <stdexcept>

// Same wire format as server/src/Native/UpcreateCompiler.cpp.
// Jump tables encode (next - at) ^ 1 per entry and terminate with a raw 1.

namespace {
    constexpr int GROUP_RELATIONS = 0;
    constexpr int GROUP_BARREL = 2;
    constexpr int GROUP_PHYSICS = 3;
    constexpr int GROUP_HEALTH = 4;
    constexpr int GROUP_ARENA = 7;
    constexpr int GROUP_NAME = 8;
    constexpr int GROUP_CAMERA = 9;
    constexpr int GROUP_POSITION = 10;
    constexpr int GROUP_STYLE = 11;
    constexpr int GROUP_SCORE = 13;
    constexpr int GROUP_TEAM = 14;
    constexpr uint32_t SCENE_STYLE_FLAGS = StyleFlags::renderFirst | StyleFlags::showsAboveParent;

    void setLevelbarMax(CameraData& c, float v) {
        c.levelbarMax = v;
        c.levelbarFill |= c.levelbarMaxSeen;
        c.levelbarMaxSeen = true;
    }

    template <typename F>
    void readSubTable(Reader& r, int max, F&& fn) {
        int at = -1;
        for (;;) {
            uint8_t b = r.u8();
            if (b == 1) return;
            at += (int)(b ^ 1);
            if (at < 0 || at >= max) throw std::range_error("subtable index out of range");
            fn(at);
        }
    }
}

EntityId Simulation::entid(Reader& r) {
    EntityId ref;
    ref.hash = r.vu();
    if (ref.hash == 0) return ref;
    ref.id = r.vu();
    return ref;
}

Entity* Simulation::get(EntityId ref) {
    if (!ref) return nullptr;
    auto it = entities.find(ref.id);
    if (it == entities.end() || it->second.hash != ref.hash) return nullptr;
    return &it->second;
}

void Simulation::decodeUpdate(Reader& r, double now) {
    tick = r.vu();
    lastUpdateNow = now;
    lastCreated = lastUpdated = lastDeleted = 0;

    uint32_t deleteCount = r.vu();
    for (uint32_t i = 0; i < deleteCount; ++i) {
        EntityId ref = entid(r);
        if (!ref) continue;
        auto it = entities.find(ref.id);
        if (it != entities.end() && it->second.hash == ref.hash) { entities.erase(it); sceneDirty = true; ++lastDeleted; }
    }

    uint32_t upcreateCount = r.vu();
    for (uint32_t i = 0; i < upcreateCount; ++i) {
        EntityId ref = entid(r);
        if (!ref) throw std::range_error("null entity in upcreate");
        uint8_t isCreation = r.u8();
        if (isCreation == 1) {
            sceneDirty = true;
            Entity& e = entities[ref.id];
            e = Entity{};
            e.id = ref.id;
            e.hash = ref.hash;
            e.createdTime = now;
            ++lastCreated;
            decodeCreation(r, e, now);
            if (e.camera) cameraId = (int)e.id;
            if (e.arena) arenaId = (int)e.id;
        } else if (isCreation == 0) {
            if (r.u8() != 1) throw std::range_error("bad update marker");
            auto it = entities.find(ref.id);
            if (it == entities.end() || it->second.hash != ref.hash) throw std::range_error("update for unknown entity");
            ++lastUpdated;
            decodeEntityUpdate(r, it->second, now);
        } else {
            throw std::range_error("bad upcreate flag");
        }
    }
}

void Simulation::decodeCreation(Reader& r, Entity& e, double now) {
    readSubTable(r, 15, [&](int at) {
        switch (at) {
            case GROUP_RELATIONS: e.relations.emplace(); break;
            case GROUP_BARREL: e.barrel.emplace(); break;
            case GROUP_PHYSICS: e.physics.emplace(); break;
            case GROUP_HEALTH: e.health.emplace(); break;
            case GROUP_ARENA: e.arena.emplace(); break;
            case GROUP_NAME: e.name.emplace(); break;
            case GROUP_CAMERA: e.camera.emplace(); break;
            case GROUP_POSITION: e.position.emplace(); break;
            case GROUP_STYLE: e.style.emplace(); break;
            case GROUP_SCORE: e.score.emplace(); break;
            case GROUP_TEAM: e.team.emplace(); break;
            default: throw std::range_error("unknown field group");
        }
    });

    if (e.position) e.position->y.snap((float)r.vi());
    if (e.position) e.position->x.snap((float)r.vi());
    if (e.position) e.position->angle.snap((float)(r.vi() / 64.0));
    if (e.physics) e.physics->size.snap(r.floatLE());
    if (e.camera) e.camera->player = entid(r);
    if (e.arena) e.arena->flags = r.vu();
    if (e.style) e.style->color = (int)r.vu();
    if (e.arena) for (int i = 0; i < 10; ++i) e.arena->scoreboardColors[i] = (int)r.vu();
    if (e.camera) e.camera->killedBy = r.stringNT();
    if (e.arena) e.arena->playersNeeded = r.vi();
    if (e.physics) e.physics->sides = r.vu();
    if (e.team) e.team->flags = r.vu();
    if (e.health) e.health->flags = r.vu();
    if (e.arena) for (int i = 0; i < 10; ++i) e.arena->scoreboardTanks[i] = r.vi();
    if (e.camera) e.camera->respawnLevel = r.vi();
    if (e.camera) e.camera->levelbarProgress = r.floatLE();
    if (e.camera) e.camera->spawnTick = r.vi();
    if (e.physics) e.physics->absorbtionFactor = r.floatLE();
    if (e.arena) e.arena->leaderX = r.floatLE();
    if (e.health) e.health->maxHealth.snap(r.floatLE());
    // flagsTime seeds the invuln blink phase; unset it stays frozen on one parity.
    if (e.style) { e.style->flags = r.vu(); e.style->flagsTime = now; }
    if (e.barrel) e.barrel->trapezoidDirection = r.floatLE();
    if (e.position) e.position->flags = r.vu();
    if (e.arena) for (int i = 0; i < 10; ++i) e.arena->scoreboardNames[i] = r.stringNT();
    if (e.camera) e.camera->score.snap(r.floatLE());
    if (e.team) e.team->mothershipY = r.floatLE();
    if (e.arena) for (int i = 0; i < 10; ++i) e.arena->scoreboardSuffixes[i] = r.stringNT();
    if (e.name) e.name->flags = r.vu();
    if (e.camera) e.camera->movementSpeed = r.floatLE();
    if (e.arena) e.arena->leaderY = r.floatLE();
    if (e.arena) e.arena->bottomY = r.floatLE();
    if (e.relations) e.relations->team = entid(r);
    if (e.camera) e.camera->level = r.vi();
    if (e.team) e.team->teamColor = (int)r.vu();
    if (e.camera) e.camera->FOV.snap(r.floatLE());
    if (e.camera) for (int i = 0; i < 8; ++i) e.camera->statLimits[i] = r.vi();
    if (e.arena) e.arena->leftX = r.floatLE();
    // floatLE here but vi in updates, same as the server
    if (e.arena) for (int i = 0; i < 10; ++i) e.arena->scoreboardScores[i] = r.floatLE();
    if (e.camera) for (int i = 0; i < 8; ++i) e.camera->statLevels[i] = r.vi();
    if (e.camera) e.camera->tankOverride = r.stringNT();
    if (e.camera) e.camera->tank = r.vi();
    if (e.style) e.style->borderWidth.snap((float)(r.vi() / 64.0));
    if (e.camera) e.camera->deathTick = r.vi();
    if (e.physics) e.physics->width.snap(r.floatLE());
    if (e.camera) e.camera->statsAvailable = r.vi();
    if (e.barrel) e.barrel->flags = r.vu();
    if (e.camera) setLevelbarMax(*e.camera, r.floatLE());
    if (e.name) e.name->name = r.stringNT();
    if (e.relations) e.relations->owner = entid(r);
    if (e.health) e.health->health.snap(r.floatLE());
    if (e.camera) e.camera->cameraY.snap(r.floatLE());
    if (e.style) e.style->opacity.snap(r.floatLE());
    if (e.barrel) e.barrel->reloadTime = r.floatLE();
    if (e.camera) for (int i = 0; i < 8; ++i) e.camera->statNames[i] = r.stringNT();
    if (e.camera) e.camera->cameraX.snap(r.floatLE());
    if (e.team) e.team->mothershipX = r.floatLE();
    if (e.camera) e.camera->unusedClientId = r.vu();
    if (e.relations) e.relations->parent = entid(r);
    if (e.style) e.style->zIndex = r.vu();
    if (e.camera) e.camera->flags = r.vu();
    if (e.arena) e.arena->rightX = r.floatLE();
    if (e.physics) e.physics->pushFactor = r.floatLE();
    if (e.physics) e.physics->flags = r.vu();
    if (e.arena) e.arena->scoreboardAmount = r.vu();
    if (e.arena) e.arena->ticksUntilStart.snap(r.floatLE());
    if (e.arena) e.arena->topY = r.floatLE();
    if (e.score) e.score->score.snap(r.floatLE());
}

void Simulation::decodeEntityUpdate(Reader& r, Entity& e, double now) {
    auto rel = [&]() -> RelationsData& { if (!e.relations) throw std::range_error("no relations"); return *e.relations; };
    auto bar = [&]() -> BarrelData& { if (!e.barrel) throw std::range_error("no barrel"); return *e.barrel; };
    auto phy = [&]() -> PhysicsData& { if (!e.physics) throw std::range_error("no physics"); return *e.physics; };
    auto hp = [&]() -> HealthData& { if (!e.health) throw std::range_error("no health"); return *e.health; };
    auto arn = [&]() -> ArenaData& { if (!e.arena) throw std::range_error("no arena"); return *e.arena; };
    auto nam = [&]() -> NameData& { if (!e.name) throw std::range_error("no name"); return *e.name; };
    auto cam = [&]() -> CameraData& { if (!e.camera) throw std::range_error("no camera"); return *e.camera; };
    auto pos = [&]() -> PositionData& { if (!e.position) throw std::range_error("no position"); return *e.position; };
    auto sty = [&]() -> StyleData& { if (!e.style) throw std::range_error("no style"); return *e.style; };
    auto sco = [&]() -> ScoreData& { if (!e.score) throw std::range_error("no score"); return *e.score; };
    auto tea = [&]() -> TeamData& { if (!e.team) throw std::range_error("no team"); return *e.team; };

    int at = -1;
    for (;;) {
        uint8_t b = r.u8();
        if (b == 1) return;
        at += (int)(b ^ 1);
        switch (at) {
            case 0: pos().y.setLinear((float)r.vi(), now); break;
            case 1: pos().x.setLinear((float)r.vi(), now); break;
            case 2: pos().angle.setAngle((float)(r.vi() / 64.0), now); break;
            case 3: phy().size.set(r.floatLE(), now); break;
            case 4: cam().player = entid(r); break;
            case 5: arn().flags = r.vu(); break;
            case 6: sty().color = (int)r.vu(); break;
            case 7: readSubTable(r, 10, [&](int i) { arn().scoreboardColors[i] = (int)r.vu(); }); break;
            case 8: cam().killedBy = r.stringNT(); break;
            case 9: arn().playersNeeded = r.vi(); break;
            case 10: phy().sides = r.vu(); break;
            case 11: tea().flags = r.vu(); break;
            case 12: hp().flags = r.vu(); break;
            case 13: readSubTable(r, 10, [&](int i) { arn().scoreboardTanks[i] = r.vi(); }); break;
            case 14: cam().respawnLevel = r.vi(); break;
            case 15: cam().levelbarProgress = r.floatLE(); break;
            case 16: cam().spawnTick = r.vi(); break;
            case 17: phy().absorbtionFactor = r.floatLE(); break;
            case 18: arn().leaderX = r.floatLE(); break;
            case 19: hp().maxHealth.set(r.floatLE(), now); break;
            case 20: {
                uint32_t nf = r.vu();
                uint32_t changed = nf ^ sty().flags;
                if (changed) sty().flagsTime = now;
                if (changed & SCENE_STYLE_FLAGS) sceneDirty = true;
                // Server XOR-toggles hasBeenDamaged per damage tick; any change flashes.
                if (changed & StyleFlags::hasBeenDamaged) e.lastDamageTime = now;
                sty().flags = nf;
                break;
            }
            case 22: bar().trapezoidDirection = r.floatLE(); break;
            case 23: pos().flags = r.vu(); break;
            case 24: readSubTable(r, 10, [&](int i) { arn().scoreboardNames[i] = r.stringNT(); }); break;
            case 25: cam().score.set(r.floatLE(), now); break;
            case 26: tea().mothershipY = r.floatLE(); break;
            case 27: readSubTable(r, 10, [&](int i) { arn().scoreboardSuffixes[i] = r.stringNT(); }); break;
            case 28: nam().flags = r.vu(); break;
            case 29: cam().movementSpeed = r.floatLE(); break;
            case 30: arn().leaderY = r.floatLE(); break;
            case 31: arn().bottomY = r.floatLE(); break;
            case 32: rel().team = entid(r); break;
            case 33: cam().level = r.vi(); break;
            case 34: tea().teamColor = (int)r.vu(); break;
            case 35: cam().FOV.set(r.floatLE(), now); break;
            case 36: readSubTable(r, 8, [&](int i) { cam().statLimits[i] = r.vi(); }); break;
            case 37: arn().leftX = r.floatLE(); break;
            case 38: readSubTable(r, 10, [&](int i) { arn().scoreboardScores[i] = r.vi(); }); break;
            case 39: readSubTable(r, 8, [&](int i) { cam().statLevels[i] = r.vi(); }); break;
            case 40: cam().tankOverride = r.stringNT(); break;
            case 41: cam().tank = r.vi(); break;
            case 42: sty().borderWidth.set((float)(r.vi() / 64.0), now); break;
            case 43: cam().deathTick = r.vi(); break;
            case 44: phy().width.set(r.floatLE(), now); break;
            case 45: cam().statsAvailable = r.vi(); break;
            case 46: {
                uint32_t nf = r.vu();
                // Server XOR-toggles hasShot per shot; any change restarts the kick anim.
                if ((nf ^ bar().flags) & 1) bar().lastFiredTime = now;
                bar().flags = nf;
                break;
            }
            case 47: setLevelbarMax(cam(), r.floatLE()); break;
            case 48: nam().name = r.stringNT(); break;
            case 49: rel().owner = entid(r); break;
            case 50: hp().health.set(r.floatLE(), now); break;
            case 51: cam().cameraY.setLinear(r.floatLE(), now); break;
            case 52: sty().opacity.set(r.floatLE(), now); break;
            case 53: bar().reloadTime = r.floatLE(); break;
            case 54: readSubTable(r, 8, [&](int i) { cam().statNames[i] = r.stringNT(); }); break;
            case 55: cam().cameraX.setLinear(r.floatLE(), now); break;
            case 56: tea().mothershipX = r.floatLE(); break;
            case 57: cam().unusedClientId = r.vu(); break;
            case 58: rel().parent = entid(r); sceneDirty = true; break;
            case 59: sty().zIndex = r.vu(); sceneDirty = true; break;
            case 60: cam().flags = r.vu(); break;
            case 61: arn().rightX = r.floatLE(); break;
            case 62: phy().pushFactor = r.floatLE(); break;
            case 63: phy().flags = r.vu(); break;
            case 64: arn().scoreboardAmount = r.vu(); break;
            case 65: arn().ticksUntilStart.set(r.floatLE(), now); break;
            case 66: arn().topY = r.floatLE(); break;
            case 67: sco().score.set(r.floatLE(), now); break;
            default: throw std::range_error("unknown update field");
        }
    }
}
