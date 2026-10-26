#include "Entity/Boss/AbstractBoss.h"
#include "Entity/Tank/TankBody.h"
#include "Entity/Tank/Barrel.h"
#include "Native/Arena.h"
#include "Native/Camera.h"
#include "Game.h"
#include "Util.h"
#include "Const/Enums.h"

#include <cmath>

void BossMovementControl::moveBoss() {
    double x = boss->positionData.x;
    double y = boss->positionData.y;

    if (target == BossMoveTarget::None) {
        if (x >= 0 && y >= 0) target = BossMoveTarget::BottomRight;
        else if (x <= 0 && y >= 0) target = BossMoveTarget::BottomLeft;
        else if (x <= 0 && y <= 0) target = BossMoveTarget::TopLeft;
        else target = BossMoveTarget::TopRight;
    }

    ArenaData& ad = boss->game->arena->arenaData;
    double tx, ty;
    switch (target) {
        case BossMoveTarget::BottomRight: tx = 3 * ad.rightX / 4;  ty = 3 * ad.bottomY / 4; break;
        case BossMoveTarget::BottomLeft:  tx = 3 * ad.leftX  / 4;  ty = 3 * ad.bottomY / 4; break;
        case BossMoveTarget::TopLeft:     tx = 3 * ad.leftX  / 4;  ty = 3 * ad.topY    / 4; break;
        default:                          tx = 3 * ad.rightX / 4;  ty = 3 * ad.topY    / 4; break;
    }

    double dx = tx - x;
    double dy = ty - y;
    double dist = dx * dx + dy * dy;
    if (dist < 90000.0) {
        target = (BossMoveTarget)(((int)target + 1) % 4);
    } else {
        double angle = std::atan2(dy, dx);
        boss->inputs->movement.x = std::cos(angle);
        boss->inputs->movement.y = std::sin(angle);
    }
}

AbstractBoss::AbstractBoss(GameServer* game)
    : LivingEntity(game), nameData(this), movementControl(this) {

    
    cameraEntity = (CameraEntity*)(ObjectEntity*)this;

    relationsData.setTeam((Entity*)cameraEntity);

    physicsData.absorbtionFactor = 0.05;
    positionData.setFlags(positionData.flags | PositionFlags::absoluteRotation);
    scoreReward = 30000.0 * game->arena->shapeScoreRewardMultiplier;
    damagePerTick = 10;

    ai = new AI(this);
    ai->viewRange = 2000;
    ai->setFindTargetInterval(0);
    inputs = ai->inputs;

    styleData.setColor(Color::Fallen);
    physicsData.setSides(1);
    physicsData.setSize(50);

    reloadTime = 15.0 * std::pow(0.914, 7.0);

    healthData.setHealth(3000);
    healthData.setMaxHealth(3000);

    entityTags |= EntityTags::isBoss;
    ObjectEntity::inputs = inputs;
}

void AbstractBoss::moveAroundMap() {
    movementControl.moveBoss();
}

void AbstractBoss::onDeath(LivingEntity* killer) {
    std::string killerName = "an unnamed tank";
    if (TankBody::isTank(killer) || AbstractBoss::isBoss(killer)) {
        NameData* nd = killer->getNameData();
        if (nd && !nd->name.empty()) killerName = nd->name;
    }

    const std::string bossName = !altName.empty() ? altName : nameData.name;
    game->broadcast()
        .u8(ClientBound::Notification)
        .stringNT("The " + bossName + " has been defeated by " + killerName + "!")
        .u32(0x000000)
        .floatLE(10000)
        .stringNT("").send();
}

void AbstractBoss::tick(int tick) {
    if (inputs != ai->inputs) {
        inputs = ai->inputs;
        ObjectEntity::inputs = inputs;
    }

    ai->movementSpeed = movementSpeed;

    if (ai->state != AIState::possessed) {
        moveAroundMap();
    } else {
        double x = positionData.x;
        double y = positionData.y;
        positionData.setAngle(std::atan2(ai->inputs->mouse.y - y, ai->inputs->mouse.x - x));
    }

    velocity.x += inputs->movement.x * movementSpeed;
    velocity.y += inputs->movement.y * movementSpeed;

    inputs->movement.x = 0;
    inputs->movement.y = 0;

    regenPerTick = healthData.maxHealth / 25000.0;

    if (!hasBeenWelcomed) {
        std::string message = "An unnamed boss has spawned!";
        if (!nameData.name.empty()) {
            const std::string bossName = !altName.empty() ? altName : nameData.name;
            message = "The " + bossName + " has spawned!";
        }
        game->broadcast().u8(ClientBound::Notification).stringNT(message).u32(0x000000).floatLE(10000).stringNT("").send();
        hasBeenWelcomed = true;
    }

    LivingEntity::tick(tick);
}
