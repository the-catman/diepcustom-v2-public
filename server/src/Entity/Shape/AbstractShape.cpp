#include "Entity/Shape/AbstractShape.h"
#include "Native/Arena.h"
#include "Game.h"
#include "Const/Enums.h"
#include "Util.h"

#include <cmath>

static constexpr int TURN_TIMEOUT = 300;

AbstractShape::AbstractShape(GameServer* game, double baseRotation, double baseOrbit, double baseVelocity)
    : LivingEntity(game), nameData(this)
{
    relationsData.setTeam(game->arena);

    nameData.flags = NameFlags::hiddenName;
    positionData.flags |= PositionFlags::absoluteRotation;

    orbitAngle = positionData.angle = util::randomFloat() * util::PI2;

    orbitRate    = (util::randomFloat() < 0.5 ? -1.0 : 1.0) * baseOrbit;
    rotationRate = (util::randomFloat() < 0.5 ? -1.0 : 1.0) * baseRotation;
    shapeVelocity = baseVelocity;

    maxDamageMultiplier = 4.0;

    entityTags |= EntityTags::isShape;
}

void AbstractShape::turnTo(double angle) {
    if (util::normalizeAngle(orbitAngle - angle) < 0.20) return;
    targetTurningAngle = angle;
    isTurning = TURN_TIMEOUT;
}

void AbstractShape::tick(int tick) {
    if (!doIdleRotate) {
        LivingEntity::tick(tick);
        return;
    }

    double x = positionData.x;
    double y = positionData.y;

    ArenaData& arena = game->arena->arenaData;

    if (isTurning == 0) {
        if (x > arena.rightX - 400 || x < arena.leftX + 400
            || y < arena.topY + 400 || y > arena.bottomY - 400) {
            turnTo(M_PI + std::atan2(y, x));
        } else if (x > arena.rightX - 500) {
            turnTo((orbitRate >= 0 ? 1.0 : -1.0) * M_PI / 2);
        } else if (x < arena.leftX + 500) {
            turnTo((orbitRate >= 0 ? -1.0 : 1.0) * M_PI / 2);
        } else if (y < arena.topY + 500) {
            turnTo(orbitRate > 0 ? 0.0 : M_PI);
        } else if (y > arena.bottomY - 500) {
            turnTo(orbitRate > 0 ? M_PI : 0.0);
        }
    }

    positionData.setAngle(positionData.angle + rotationRate);
    orbitAngle += orbitRate + (isTurning == TURN_TIMEOUT ? orbitRate * 10.0 : 0.0);

    if (isTurning == TURN_TIMEOUT) {
        double diff = std::fmod(std::fmod(orbitAngle - targetTurningAngle, util::PI2) + util::PI2, util::PI2);
        if (diff < 0.20) isTurning -= 1;
    } else if (isTurning != 0) {
        isTurning -= 1;
    }

    maintainVelocity(orbitAngle, shapeVelocity);

    LivingEntity::tick(tick);
}
