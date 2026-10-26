#include "Entity/AI.h"
#include "Entity/Object.h"
#include "Native/Arena.h"
#include "Native/Manager.h"
#include "Physics/PackedEntitySet.h"
#include "Physics/CollisionManager.h"
#include "Game.h"
#include "Util.h"

#include <algorithm>
#include <cmath>
#include <limits>

int AI::_aiHashCounter = 0;

AI::AI(ObjectEntity* owner, bool claimable) : owner(owner), game(owner->game) {
    passiveRotation = util::randomFloat() < 0.5 ? PASSIVE_ROTATION : -PASSIVE_ROTATION;
    inputs = std::make_shared<Inputs>();
    _aiHash = (_aiHashCounter++) % 16384;

    inputs->mouse.set({0, 0});
    targetFilter = [](const VectorAbstract&) { return true; };
    if (claimable) isClaimable = true;

    game->entities->AIs.push_back(this);
}

AI::~AI() {
    auto& v = game->entities->AIs;
    auto it = std::find(v.begin(), v.end(), this);
    if (it != v.end()) v.erase(it);
}

ObjectEntity* AI::findTarget(int tick) {
    if (_findTargetInterval != 0 && ((tick + _aiHash) % _findTargetInterval) != 1) {
        return Entity::exists(target) ? target : (target = nullptr);
    }

    PositionData& rootPos = owner->rootParent->positionData;
    Entity* team = owner->relationsData.team;

    if (Entity::exists(target)) {
        if (team != target->relationsData.team && target->physicsData.sides != 0 && target->styleData.opacity > 0) {
            double dx = target->positionData.x - rootPos.x;
            double dy = target->positionData.y - rootPos.y;
            double targetDistSq = dx * dx + dy * dy;
            if (targetFilter(VectorAbstract{target->positionData.x, target->positionData.y}) && targetDistSq < (viewRange * viewRange) * 2) return target;
        }
    }

    ObjectEntity* root = owner->rootParent;

    PackedEntitySet& entities = (viewRange == std::numeric_limits<double>::infinity())
        ? PackedEntitySet::fullSet()
        : game->entities->collisionManager->retrieve(root->positionData.x, root->positionData.y, viewRange, viewRange);

    ObjectEntity* closestPlayer = nullptr;
    double closestPlayerDistSq = viewRange * viewRange;
    ObjectEntity* closestEntity = nullptr;
    double closestDistSq = viewRange * viewRange;

    // fullSet() covers all 16384 slots; only words up to lastId can hold live entities.
    size_t words = entities.data.size();
    int lastId = game->entities->lastId;
    size_t liveWords = lastId < 0 ? 0 : (size_t)(lastId / 32) + 1;
    if (liveWords < words) words = liveWords;

    for (size_t i = 0; i < words; ++i) {
        uint32_t chunk = entities.data[i];
        while (chunk) {
            uint32_t bitValue = chunk & (0u - chunk);
            int bitIdx = 31 - __builtin_clz(bitValue);
            chunk ^= bitValue;
            int id = 32 * (int)i + bitIdx;

            Entity* e = game->entities->inner[id];
            if (!e || e->hash == 0) continue;
            if (!ObjectEntity::isObject(e)) continue;
            ObjectEntity* entity = (ObjectEntity*)e;

            if (!entity->isPhysical) continue;
            if (targetFilterNonLiving && !entity->getHealthData()) continue;
            if (entity->physicsData.flags & PhysicsFlags::isBase) continue;
            auto& ownerRef = entity->relationsData.owner;
            if (ownerRef.rawHash() != 0) {
                Entity* o = ownerRef.get();
                if (!o || o->getPositionData()) continue;
            }
            if (entity->relationsData.team == team) continue;
            if (!targetArenaTeam && entity->relationsData.team == game->arena) continue;
            if (entity->physicsData.sides == 0) continue;
            if (entity->styleData.opacity <= 0) continue;
            if (!targetFilter(VectorAbstract{entity->positionData.x, entity->positionData.y})) continue;

            double dX = entity->positionData.x - rootPos.x;
            double dY = entity->positionData.y - rootPos.y;
            double distSq = dX * dX + dY * dY;
            if (entity->entityTags & EntityTags::isTank) {
                if (distSq < closestPlayerDistSq) {
                    closestPlayer = entity;
                    closestPlayerDistSq = distSq;
                }
            } else if (distSq < closestDistSq) {
                closestEntity = entity;
                closestDistSq = distSq;
            }
        }
    }

    return target = closestPlayer ? closestPlayer : closestEntity;
}

void AI::aimAtTarget() {
    if (!target) return;

    double moveSpeed = aimSpeed * 1.6;
    Vector ownerPos = owner->getWorldPosition();

    VectorAbstract pos{target->positionData.x, target->positionData.y};

    if (moveSpeed <= 0.001) {
        inputs->movement.set({pos.x - ownerPos.x, pos.y - ownerPos.y});
        inputs->mouse.set(pos);
        inputs->movement.setMagnitude(1);
        return;
    }
    if (doAimPrediction) {
        VectorAbstract delta{pos.x - ownerPos.x, pos.y - ownerPos.y};
        double dist = std::sqrt(delta.x * delta.x + delta.y * delta.y);
        if (dist == 0) dist = 1;

        VectorAbstract unitPerp{delta.y / dist, -delta.x / dist};
        double entPerp = unitPerp.x * target->velocity.x + unitPerp.y * target->velocity.y;

        if (entPerp > moveSpeed * 0.9) entPerp = moveSpeed * 0.9;
        if (entPerp < moveSpeed * -0.9) entPerp = moveSpeed * -0.9;

        double directComponent = std::sqrt(moveSpeed * moveSpeed - entPerp * entPerp);
        double offset = (entPerp / directComponent * dist) / 2;

        inputs->mouse.set({pos.x + offset * unitPerp.x, pos.y + offset * unitPerp.y});
    } else {
        inputs->mouse.set({pos.x, pos.y});
    }

    inputs->movement.setMagnitude(1);
    inputs->movement.setAngle(std::atan2(inputs->mouse.y - ownerPos.y, inputs->mouse.x - ownerPos.x));
}

void AI::tick(int tick) {
    if (state == AIState::possessed) {
        if (!inputs->deleted) return;
        inputs = std::make_shared<Inputs>();
    }

    ObjectEntity* target = findTarget(tick);

    if (!target) {
        inputs->flags = 0;
        state = AIState::idle;
        double angle = inputs->mouse.angle() + passiveRotation;
        inputs->mouse.set({std::cos(angle) * 100, std::sin(angle) * 100});
    } else {
        state = AIState::hasTarget;
        inputs->flags |= InputFlags::leftclick;
        aimAtTarget();
    }
}
