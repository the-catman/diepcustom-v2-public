#include "Entity/Misc/ArenaCloser.h"
#include "Entity/Tank/Barrel.h"
#include "Native/Arena.h"
#include "Native/Camera.h"
#include "Game.h"
#include "Const/Enums.h"
#include "Config.h"

#include <cmath>
#include <limits>

ArenaCloser::ArenaCloser(GameServer* game)
    : TankBody(game, new CameraEntity(game), std::make_shared<Inputs>()) {
    arenaMobID = "ArenaCloser";
    cameraEntity->cameraData.setPlayer(this);
    cameraEntity->setLevel(300);

    scaleFactor = 1;
    scale(BASE_SIZE / baseSize);

    relationsData.setTeam(game->arena);

    ai = new AI(this);
    ai->inputs = inputs;
    ai->viewRange = std::numeric_limits<double>::infinity();

    setTank(Tank::ArenaCloser);

    nameData.setName("Arena Closer");
    styleData.setColor(Color::Neutral);
    positionData.setFlags(positionData.flags | PositionFlags::canMoveThroughWalls);
    physicsData.setFlags(physicsData.flags | PhysicsFlags::canEscapeArena);

    for (int i = Stat::MovementSpeed; i < Stat::BodyDamage; ++i) cameraEntity->setStat(i, 7);

    if (!barrels.empty()) ai->aimSpeed = barrels[0]->bulletAccel * 1.6;
    setInvulnerability(true);

    ai->movementSpeed = cameraEntity->cameraData.movementSpeed = 5;
    healthData.health = 10000;
    healthData.maxHealth = 10000;
    damagePerTick = 45;
}

void ArenaCloser::tick(int tick) {
    inputs = ai->inputs;

    if (ai->state == AIState::idle) {
        double angle = positionData.angle + ai->passiveRotation;
        double dx = inputs->mouse.x - positionData.x;
        double dy = inputs->mouse.y - positionData.y;
        double mag = std::sqrt(dx * dx + dy * dy);
        inputs->mouse.x = positionData.x + std::cos(angle) * mag;
        inputs->mouse.y = positionData.y + std::sin(angle) * mag;
    }

    TankBody::tick(tick);
}
