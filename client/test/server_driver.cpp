#include "ProtocolTest.h"

#include "Client.h"
#include "Config.h"
#include "Const/Enums.h"
#include "Const/TankDefinitions.h"
#include "Game.h"
#include "Gamemodes/FFA.h"
#include "Native/Camera.h"
#include "Native/FieldGroups.h"
#include "Native/UpcreateCompiler.h"

#include <memory>

namespace {
    class CaptureWriter : public Writer {
    public:
        std::vector<uint8_t> take() { return std::move(buffer); }
    };

    class FixtureEntity : public Entity {
    public:
        RelationsData relationsData;
        BarrelData barrelData;
        PhysicsData physicsData;
        HealthData healthData;
        ArenaData arenaData;
        NameData nameData;
        CameraData cameraData;
        PositionData positionData;
        StyleData styleData;
        ScoreData scoreData;
        TeamData teamData;
        int group;

        FixtureEntity(GameServer* game, int group)
            : Entity(game), relationsData(this), barrelData(this), physicsData(this), healthData(this),
              arenaData(this), nameData(this), cameraData(this), positionData(this), styleData(this),
              scoreData(this), teamData(this), group(group) {}

        RelationsData* getRelationsData() override { return group == 0 ? &relationsData : nullptr; }
        BarrelData* getBarrelData() override { return group == 1 ? &barrelData : nullptr; }
        PhysicsData* getPhysicsData() override { return group == 2 ? &physicsData : nullptr; }
        HealthData* getHealthData() override { return group == 3 ? &healthData : nullptr; }
        ArenaData* getArenaData() override { return group == 4 ? &arenaData : nullptr; }
        NameData* getNameData() override { return group == 5 ? &nameData : nullptr; }
        CameraData* getCameraData() override { return group == 6 ? &cameraData : nullptr; }
        PositionData* getPositionData() override { return group == 7 ? &positionData : nullptr; }
        StyleData* getStyleData() override { return group == 8 ? &styleData : nullptr; }
        ScoreData* getScoreData() override { return group == 9 ? &scoreData : nullptr; }
        TeamData* getTeamData() override { return group == 10 ? &teamData : nullptr; }
    };

    void initialize() {
        static bool initialized = false;
        if (initialized) return;
        config::init();
        initTankDefinitions();
        initialized = true;
    }
}

std::vector<std::vector<uint8_t>> runServerSession(const std::vector<InjectedPacket>& injected, int ticks) {
    initialize();

    GameServer* game = new GameServer([](GameServer* owner) { return new FFAArena(owner); }, "ffa", "FFA");
    Client* client = new Client(game);
    std::vector<std::vector<uint8_t>> packets;
    client->sendRaw = [&](const uint8_t* data, size_t size) { packets.emplace_back(data, data + size); };
    client->closeWs = []() {};

    size_t nextPacket = 0;
    for (int tick = 0; tick < ticks; ++tick) {
        while (nextPacket < injected.size() && injected[nextPacket].tick <= tick) {
            const std::vector<uint8_t>& data = injected[nextPacket].data;
            client->onMessage(data.data(), data.size(), true);
            ++nextPacket;
        }
        game->tickLoop();
    }
    return packets;
}

ProtocolFixtures compileProtocolFixtures() {
    initialize();

    GameServer* game = new GameServer([](GameServer* owner) { return new FFAArena(owner); }, "ffa", "FFA");
    CameraEntity* viewer = new CameraEntity(game);
    std::vector<FixtureEntity*> entities;
    for (int group = 0; group <= 10; ++group) entities.push_back(new FixtureEntity(game, group));
    FixtureEntity* nullParent = new FixtureEntity(game, 0);
    entities.push_back(nullParent);

    FixtureEntity* relations = entities[0];
    FixtureEntity* barrel = entities[1];
    FixtureEntity* physics = entities[2];
    FixtureEntity* health = entities[3];
    FixtureEntity* arena = entities[4];
    FixtureEntity* name = entities[5];
    FixtureEntity* camera = entities[6];
    FixtureEntity* position = entities[7];
    FixtureEntity* style = entities[8];
    FixtureEntity* score = entities[9];
    FixtureEntity* team = entities[10];

    relations->relationsData.parent = position;
    barrel->barrelData.flags = BarrelFlags::hasShot;
    barrel->barrelData.reloadTime = 20;
    barrel->barrelData.trapezoidDirection = 0.5;
    physics->physicsData.flags = PhysicsFlags::isTrapezoid | PhysicsFlags::showsOnMap |
        PhysicsFlags::doChildrenCollision | PhysicsFlags::noOwnTeamCollision | PhysicsFlags::isSolidWall |
        PhysicsFlags::onlySameOwnerCollision | PhysicsFlags::isBase | PhysicsFlags::canEscapeArena;
    physics->physicsData.sides = 5;
    physics->physicsData.size = 42;
    physics->physicsData.width = 21;
    health->healthData.health = 25;
    health->healthData.maxHealth = 100;
    arena->arenaData.flags = ArenaFlags::noJoining | ArenaFlags::showsLeaderArrow | ArenaFlags::hiddenScores |
        ArenaFlags::gameReadyStart | ArenaFlags::canUseCheats;
    arena->arenaData.leftX = -5000;
    arena->arenaData.topY = -4000;
    arena->arenaData.rightX = 5000;
    arena->arenaData.bottomY = 4000;
    arena->arenaData.scoreboardAmount = 1;
    arena->arenaData.scoreboardNames.values[0] = "Fixture";
    arena->arenaData.scoreboardScores.values[0] = 1234;
    arena->arenaData.scoreboardColors.values[0] = Color::TeamBlue;
    arena->arenaData.scoreboardSuffixes.values[0] = "pts";
    arena->arenaData.scoreboardTanks.values[0] = Tank::Basic;
    arena->arenaData.leaderX = 100;
    arena->arenaData.leaderY = -200;
    arena->arenaData.playersNeeded = 3;
    arena->arenaData.ticksUntilStart = 75;
    name->nameData.flags = NameFlags::highlightedName;
    name->nameData.name = "Fixture";
    camera->cameraData.flags = CameraFlags::showingDeathStats;
    camera->cameraData.player = position;
    camera->cameraData.FOV = 0.5;
    camera->cameraData.level = 12;
    camera->cameraData.tank = Tank::Sniper;
    camera->cameraData.cameraX = 123;
    camera->cameraData.cameraY = 456;
    camera->cameraData.score = 5000;
    camera->cameraData.respawnLevel = 4;
    camera->cameraData.killedBy = "Fixture";
    camera->cameraData.spawnTick = 10;
    camera->cameraData.deathTick = 20;
    camera->cameraData.movementSpeed = 3.5;
    camera->cameraData.levelbarProgress = 50;
    camera->cameraData.levelbarMax = 100;
    camera->cameraData.statsAvailable = 2;
    camera->cameraData.tankOverride = "Fixture Tank";
    for (int i = 0; i < 8; ++i) {
        camera->cameraData.statNames.values[i] = "Stat" + std::to_string(i);
        camera->cameraData.statLevels.values[i] = i;
        camera->cameraData.statLimits.values[i] = 7;
    }
    position->positionData.x = 125;
    position->positionData.y = -250;
    position->positionData.angle = 1.25;
    position->positionData.flags = PositionFlags::absoluteRotation | PositionFlags::canMoveThroughWalls;
    style->styleData.flags = StyleFlags::isVisible | StyleFlags::hasBeenDamaged | StyleFlags::isFlashing |
        StyleFlags::renderFirst | StyleFlags::isStar | StyleFlags::isCachable |
        StyleFlags::showsAboveParent | StyleFlags::hasNoDmgIndicator;
    style->styleData.color = Color::EnemyPentagon;
    style->styleData.borderWidth = 5;
    style->styleData.opacity = 0.75;
    style->styleData.zIndex = 8;
    score->scoreData.score = 1234;
    team->teamData.flags = TeamFlags::hasMothership;
    team->teamData.teamColor = Color::TeamRed;
    team->teamData.mothershipX = 250;
    team->teamData.mothershipY = -350;

    CaptureWriter creation;
    creation.vu(1).vu(0).vu((int32_t)entities.size());
    for (FixtureEntity* entity : entities) compileCreation(viewer, creation, entity);

    relations->relationsData.setParent(nullptr);
    barrel->barrelData.setFlags(0);
    barrel->barrelData.setReloadTime(25);
    physics->physicsData.setFlags(PhysicsFlags::showsOnMap | PhysicsFlags::canEscapeArena);
    physics->physicsData.setSize(48);
    health->healthData.setHealth(50);
    arena->arenaData.setFlags(ArenaFlags::showsLeaderArrow);
    arena->arenaData.setLeaderX(300);
    name->nameData.setName("Updated");
    camera->cameraData.setFlags(camera->cameraData.flags | CameraFlags::usesCameraCoords);
    camera->cameraData.setCameraX(321);
    camera->cameraData.setCameraY(654);
    camera->cameraData.setScore(6000);
    camera->cameraData.setLevelbarProgress(75);
    camera->cameraData.setLevelbarMax(125);
    position->positionData.setX(200);
    position->positionData.setFlags(PositionFlags::canMoveThroughWalls);
    style->styleData.setFlags(StyleFlags::isVisible | StyleFlags::hasNoDmgIndicator);
    style->styleData.setOpacity(0.5);
    score->scoreData.setScore(2345);
    team->teamData.setMothershipX(400);
    nullParent->relationsData.setParent(position);

    CaptureWriter update;
    update.vu(2).vu(0).vu((int32_t)entities.size());
    for (FixtureEntity* entity : entities) compileUpdate(viewer, update, entity);

    return {creation.take(), update.take()};
}
