#include "ProtocolTest.h"

#include "Const.h"
#include "coder/Reader.h"
#include "sim/Simulation.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

namespace {
    int failures = 0;

    class PacketWriter {
    public:
        std::vector<uint8_t> data;

        PacketWriter& u8(int value) { data.push_back((uint8_t)value); return *this; }
        PacketWriter& vu(uint32_t value) {
            do {
                uint8_t part = value & 0x7f;
                value >>= 7;
                if (value) part |= 0x80;
                data.push_back(part);
            } while (value);
            return *this;
        }
        PacketWriter& vi(int32_t value) {
            uint32_t sign = (uint32_t)(0 - (value < 0 ? 1 : 0));
            return vu(sign ^ ((uint32_t)value << 1));
        }
        PacketWriter& vf(float value) {
            uint32_t bits;
            std::memcpy(&bits, &value, 4);
            bits = ((bits & 0xff) << 24) | ((bits & 0xff00) << 8) | ((bits >> 8) & 0xff00) | ((bits >> 24) & 0xff);
            return vi((int32_t)bits);
        }
        PacketWriter& stringNT(const std::string& value) {
            data.insert(data.end(), value.begin(), value.end());
            data.push_back(0);
            return *this;
        }
    };

    void check(bool condition, const char* message) {
        if (condition) return;
        std::printf("FAIL: %s\n", message);
        ++failures;
    }

    InjectedPacket initPacket() {
        PacketWriter writer;
        writer.u8(ServerBound::Init).stringNT(config::BUILD_HASH).stringNT("");
        return {0, std::move(writer.data)};
    }

    InjectedPacket spawnPacket() {
        PacketWriter writer;
        writer.u8(ServerBound::Spawn).stringNT("roundtrip");
        return {1, std::move(writer.data)};
    }

    InjectedPacket inputPacket(int tick, uint32_t flags, int x, int y) {
        PacketWriter writer;
        writer.u8(ServerBound::Input).vu(flags).vf((float)x).vf((float)y);
        return {tick, std::move(writer.data)};
    }

    void decodePacket(Simulation& simulation, const std::vector<uint8_t>& packet, double now) {
        Reader reader(packet.data(), packet.size());
        simulation.decodeUpdate(reader, now);
        check(reader.done(), "update fully consumed");
    }

    std::vector<uint8_t> styleUpdatePacket(uint32_t hash, uint32_t id, uint32_t flags) {
        PacketWriter writer;
        writer.vu(1).vu(0).vu(1).vu(hash).vu(id).u8(0).u8(1).u8((20 - -1) ^ 1).vu(flags).u8(1);
        return std::move(writer.data);
    }

    void testSceneInvalidation() {
        Simulation simulation;
        Entity& entity = simulation.entities[1];
        entity.id = 1;
        entity.hash = 2;
        entity.style.emplace();

        simulation.sceneDirty = false;
        decodePacket(simulation, styleUpdatePacket(2, 1, StyleFlags::isVisible | StyleFlags::hasBeenDamaged), 10);
        check(!simulation.sceneDirty, "damage flag does not dirty scene");
        check(entity.style->flagsTime == 10, "damage flag updates transition time");
        check(entity.lastDamageTime == 10, "damage flag updates damage time");

        simulation.sceneDirty = false;
        decodePacket(simulation, styleUpdatePacket(2, 1, StyleFlags::isVisible | StyleFlags::renderFirst), 20);
        check(simulation.sceneDirty, "render-first flag dirties scene");

        simulation.sceneDirty = false;
        decodePacket(simulation, styleUpdatePacket(2, 1,
            StyleFlags::isVisible | StyleFlags::renderFirst | StyleFlags::showsAboveParent), 30);
        check(simulation.sceneDirty, "above-parent flag dirties scene");
    }

    void testFixtures() {
        ProtocolFixtures fixtures = compileProtocolFixtures();
        Simulation simulation;
        Reader creation(fixtures.creationPacket.data(), fixtures.creationPacket.size());
        simulation.decodeUpdate(creation, 0);
        check(creation.done(), "fixture creation fully consumed");
        check(simulation.entities.size() == 12, "fixture creates every record kind");

        int relationCount = 0;
        int componentCount = 0;
        for (auto& pair : simulation.entities) {
            Entity& entity = pair.second;
            relationCount += entity.relations.has_value();
            componentCount += entity.relations.has_value() + entity.barrel.has_value() + entity.physics.has_value() +
                entity.health.has_value() + entity.arena.has_value() + entity.name.has_value() +
                entity.camera.has_value() + entity.position.has_value() + entity.style.has_value() +
                entity.score.has_value() + entity.team.has_value();
            if (entity.physics) check(entity.physics->flags == 0x17f, "physics flags raw");
            if (entity.position) check(entity.position->flags == 3, "position flags raw");
            if (entity.style) check(entity.style->flags == 0xff, "style flags raw");
            if (entity.arena) {
                check(entity.arena->flags == 0x1f, "arena flags raw");
                check(entity.arena->scoreboardScores[0] == 1234, "scoreboard score creation decoded");
            }
            if (entity.health) {
                check(entity.health->health.target == 25, "health decoded");
                check(entity.health->maxHealth.target == 100, "max health decoded");
            }
        }
        check(relationCount == 2, "fixture includes null and non-null relations");
        check(componentCount == 12, "fixture entities each decode one component");

        Entity* camera = simulation.getCamera();
        check(camera && camera->camera, "fixture camera decoded");
        if (camera && camera->camera) {
            check(camera->camera->level == 12, "camera level decoded");
            check(camera->camera->tank == 6, "camera tank decoded");
            check(camera->camera->cameraX.target == 123, "camera X decoded");
            check(camera->camera->cameraY.target == 456, "camera Y decoded");
            check(camera->camera->levelbarProgress == 50, "levelbar progress decoded");
            check(camera->camera->levelbarMax == 100, "levelbar max decoded");
            check(camera->camera->movementSpeed == 3.5, "movement speed decoded");
        }

        Reader update(fixtures.updatePacket.data(), fixtures.updatePacket.size());
        simulation.decodeUpdate(update, 100);
        check(update.done(), "fixture update fully consumed");
        camera = simulation.getCamera();
        if (camera && camera->camera) {
            check(camera->camera->cameraX.target == 321, "camera X transition decoded");
            check(camera->camera->cameraY.target == 654, "camera Y transition decoded");
            check(camera->camera->score.target == 6000, "camera score update decoded");
            check(camera->camera->levelbarProgress == 75, "levelbar progress update decoded");
            check(camera->camera->levelbarMax == 125, "levelbar max update decoded");
        }
        int parentCount = 0;
        for (auto& pair : simulation.entities) {
            Entity& entity = pair.second;
            if (entity.relations && entity.relations->parent) ++parentCount;
            if (entity.physics) check(entity.physics->flags == 0x102, "physics flag update raw");
            if (entity.position) check(entity.position->flags == 2, "position flag update raw");
            if (entity.style) check(entity.style->flags == 0x81, "style flag update raw");
            if (entity.arena) check(entity.arena->flags == 2, "arena flag update raw");
            if (entity.health) check(entity.health->health.target == 50, "health update decoded");
        }
        check(parentCount == 1, "null and non-null parent updates decoded");

        std::printf("fixtures: %zu creation bytes, %zu update bytes\n",
            fixtures.creationPacket.size(), fixtures.updatePacket.size());
    }

    void testSession() {
        std::vector<InjectedPacket> injected;
        injected.push_back(initPacket());
        injected.push_back(spawnPacket());
        injected.push_back(inputPacket(24, (1u << 3) | (1u << 4), 10000, 10000));
        injected.push_back(inputPacket(120, 0, 10000, 10000));

        std::vector<std::vector<uint8_t>> packets = runServerSession(injected, 200);
        Simulation simulation;
        bool gotServerInfo = false;
        bool gotAccept = false;
        int updateCount = 0;
        size_t updateBytes = 0;
        for (const std::vector<uint8_t>& packet : packets) {
            if (packet.empty()) continue;
            if (packet[0] == ClientBound::ServerInfo) gotServerInfo = true;
            if (packet[0] == ClientBound::Accept) gotAccept = true;
            if (packet[0] != ClientBound::Update) continue;
            Reader reader(packet.data() + 1, packet.size() - 1);
            simulation.decodeUpdate(reader, updateCount * 40.0);
            check(reader.done(), "session update fully consumed");
            ++updateCount;
            updateBytes += packet.size();
        }

        check(gotServerInfo, "server info received");
        check(gotAccept, "accept received");
        check(updateCount == 200, "one update received per tick");
        check(simulation.getArena() && simulation.getArena()->arena, "session arena decoded");
        check(simulation.getCamera() && simulation.getCamera()->camera, "session camera decoded");
        Entity* camera = simulation.getCamera();
        Entity* player = camera && camera->camera ? simulation.get(camera->camera->player) : nullptr;
        check(player && player->position && player->physics && player->style, "session player decoded");
        check(player && player->name && player->name->name == "roundtrip", "session player name decoded");

        std::printf("session: %d updates, %zu update bytes\n", updateCount, updateBytes);
    }
}

int main() {
    testSceneInvalidation();
    testFixtures();
    testSession();
    if (failures) {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::puts("protocol tests OK");
    return 0;
}
