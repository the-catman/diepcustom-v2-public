#include "Entity/Object.h"
#include "Native/Arena.h"
#include "Native/Manager.h"
#include "Game.h"
#include "Util.h"
#include "Const/Enums.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

void DeletionAnimation::tick() {
    if (frame == -1) throw std::runtime_error("Animation failed. Entity should be gone by now");

    switch (frame) {
        case 0:
            // destroy(false) deletes this DeletionAnimation; touch nothing after.
            entity->destroy(false);
            return;
        case 5:
            entity->styleData.setOpacity(1 - (1.0 / 6));
            // fallthrough
        default:
            entity->scale(1.1);
            entity->styleData.setOpacity(entity->styleData.opacity - 1.0 / 6);
            if (entity->styleData.opacity < 0) entity->styleData.setOpacity(0);
            break;
    }
    frame -= 1;
}

ObjectEntity::ObjectEntity(GameServer* game)
    : Entity(game, EntityCategory::Object), relationsData(this), physicsData(this), positionData(this), styleData(this) {
    styleData.setZIndex(game->entities->zIndex++);
}

void ObjectEntity::handleCollision(ObjectEntity* a, ObjectEntity* b) {
    a->receiveKnockback(b);
    b->receiveKnockback(a);
}

bool ObjectEntity::isColliding(ObjectEntity* objA, ObjectEntity* objB) {
    if (objA == objB) return false;
    if (!objA->isPhysical || !objB->isPhysical) return false;
    PhysicsData& pa = objA->physicsData;
    PhysicsData& pb = objB->physicsData;
    RelationsData& ra = objA->relationsData;
    RelationsData& rb = objB->relationsData;
    PositionData& posA = objA->positionData;
    PositionData& posB = objB->positionData;

    if (pa.sides == 0) return false;
    if (pb.sides == 0) return false;
    if (objA->deletionAnimation) return false;
    if (objB->deletionAnimation) return false;

    if (ra.team.sameAs(rb.team)) {
        if ((pa.flags & PhysicsFlags::noOwnTeamCollision) || (pb.flags & PhysicsFlags::noOwnTeamCollision)) return false;
        if (!ra.owner.sameAs(rb.owner)) {
            if ((pa.flags & PhysicsFlags::onlySameOwnerCollision) || (pb.flags & PhysicsFlags::onlySameOwnerCollision)) return false;
        }
    }

    if (rb.team == (Entity*)objB->game->arena && (pa.flags & PhysicsFlags::isBase) && !(objB->entityTags & EntityTags::isBoss)) return false; // TBD

    bool isARect = pa.sides == 2;
    bool isBRect = pb.sides == 2;

    if (isARect && isBRect) {
        return false;
    } else if (isARect && !isBRect) {
        double dX = util::constrain(posB.x, posA.x - pa.size / 2, posA.x + pa.size / 2) - posB.x;
        double dY = util::constrain(posB.y, posA.y - pa.width / 2, posA.y + pa.width / 2) - posB.y;
        return dX * dX + dY * dY <= pb.size * pb.size;
    } else if (pb.sides == 2 && pa.sides != 2) {
        double dX = util::constrain(posA.x, posB.x - pb.size / 2, posB.x + pb.size / 2) - posA.x;
        double dY = util::constrain(posA.y, posB.y - pb.width / 2, posB.y + pb.width / 2) - posA.y;
        return dX * dX + dY * dY <= pa.size * pa.size;
    } else {
        double dX = posA.x - posB.x;
        double dY = posA.y - posB.y;
        double rSum = pa.size + pb.size;
        return dX * dX + dY * dY <= rSum * rSum;
    }
}

void ObjectEntity::destroy(bool animate) {
    if (!animate) {
        if (deletionAnimation) { delete deletionAnimation; deletionAnimation = nullptr; }
        delete_();
    } else if (!deletionAnimation) {
        deletionAnimation = new DeletionAnimation(this);
    }
}

void ObjectEntity::delete_() {
    if (isChild) {
        auto& siblings = rootParent->children;
        auto it = std::find(siblings.begin(), siblings.end(), this);
        if (it != siblings.end()) util::removeFast(siblings, it - siblings.begin());
    } else {
        for (ObjectEntity* child : children) {
            child->isChild = false;
            child->delete_();
        }
        children.clear();
    }

    if (physicsData.flags & PhysicsFlags::showsOnMap) {
        auto& globalEntities = game->entities->globalEntities;
        auto it = std::find(globalEntities.begin(), globalEntities.end(), id);
        if (it == globalEntities.end()) return;
        util::removeFast(globalEntities, it - globalEntities.begin());
    }

    Entity::delete_();
}

void ObjectEntity::addVelocity(double angle, double magnitude) {
    velocity.x += magnitude * std::cos(angle);
    velocity.y += magnitude * std::sin(angle);
}

void ObjectEntity::setVelocity(double angle, double magnitude) {
    velocity.x = magnitude * std::cos(angle);
    velocity.y = magnitude * std::sin(angle);
}

void ObjectEntity::maintainVelocity(double angle, double maxSpeed) {
    addVelocity(angle, maxSpeed * 0.1);
}

void ObjectEntity::applyPhysics() {
    Vector& v = velocity;
    if (v.x * v.x + v.y * v.y < 0.0001) { v.x = 0; v.y = 0; }
    else if (deletionAnimation) { v.x /= 2; v.y /= 2; }

    positionData.setX(positionData.x + v.x);
    positionData.setY(positionData.y + v.y);

    v.x *= 0.9;
    v.y *= 0.9;
}

void ObjectEntity::receiveKnockback(ObjectEntity* entity) {
    double kbMagnitude = physicsData.absorbtionFactor * entity->physicsData.pushFactor;
    double kbAngle;
    double diffY = positionData.y - entity->positionData.y;
    double diffX = positionData.x - entity->positionData.x;
    if (diffX == 0 && diffY == 0) kbAngle = util::randomFloat() * util::PI2;
    else kbAngle = std::atan2(diffY, diffX);

    if ((entity->physicsData.flags & PhysicsFlags::isSolidWall || entity->physicsData.flags & PhysicsFlags::isBase)
        && !(positionData.flags & PositionFlags::canMoveThroughWalls)) {
        if (entity->physicsData.flags & PhysicsFlags::isSolidWall) {
            Entity* owner = relationsData.owner.get();
            // unresolvable-but-set owner is usually a dead tank's bullet; keep destroying those
            bool ownerIsObject = owner ? owner->getPositionData() != nullptr : relationsData.owner.rawHash() != 0;
            if (ownerIsObject && !relationsData.team.sameAs(entity->relationsData.team)) {
                setVelocity(0, 0);
                destroy(true);
                return;
            }
            velocity.setMagnitude(velocity.magnitude() * 0.3);
        }
        kbMagnitude /= 0.3;
    }
    if (entity->physicsData.sides == 2) {
        if (positionData.flags & PositionFlags::canMoveThroughWalls) {
            kbMagnitude = 0;
        } else {
            double relA = std::cos(kbAngle + entity->positionData.angle) / entity->physicsData.size;
            double relB = std::sin(kbAngle + entity->positionData.angle) / entity->physicsData.width;
            if (std::abs(relA) <= std::abs(relB)) {
                if (relB < 0) addVelocity(M_PI * 3 / 2, kbMagnitude);
                else addVelocity(M_PI * 1 / 2, kbMagnitude);
            } else {
                if (relA < 0) addVelocity(M_PI, kbMagnitude);
                else addVelocity(0, kbMagnitude);
            }
        }
    } else {
        addVelocity(kbAngle, kbMagnitude);
    }
}

void ObjectEntity::setParent(ObjectEntity* parent) {
    relationsData.setParent(parent);
    rootParent = parent->rootParent;
    rootParent->children.push_back(this);
    isChild = true;
    isPhysical = false;
}

Vector ObjectEntity::getWorldPosition() {
    Vector pos(positionData.x, positionData.y);
    double x = pos.x;
    double y = pos.y;

    double px = 0, py = 0, par = 0;

    ObjectEntity* entity = this;
    while (ObjectEntity::isObject(entity->relationsData.parent)) {
        ObjectEntity* parent = (ObjectEntity*)entity->relationsData.parent.get();
        if (!(parent->positionData.flags & PositionFlags::absoluteRotation)) pos.setAngle(pos.angle() + entity->positionData.angle);
        entity = parent;
        px += entity->positionData.x;
        py += entity->positionData.y;
        if (entity->positionData.flags & PositionFlags::absoluteRotation) par += entity->positionData.angle;
    }

    double cos = std::cos(par);
    double sin = std::sin(par);
    pos.x = px + x * cos - y * sin;
    pos.y = py + x * sin + y * cos;
    return pos;
}

void ObjectEntity::setGlobalEntity() {
    physicsData.setFlags(physicsData.flags | PhysicsFlags::showsOnMap);
    game->entities->globalEntities.push_back(id);
}

void ObjectEntity::keepInArena() {
    ArenaData& arena = game->arena->arenaData;
    double padding = game->arena->ARENA_PADDING;

    if (positionData.x < arena.leftX - padding) positionData.setX(arena.leftX - padding);
    else if (positionData.x > arena.rightX + padding) positionData.setX(arena.rightX + padding);

    if (positionData.y < arena.topY - padding) positionData.setY(arena.topY - padding);
    else if (positionData.y > arena.bottomY + padding) positionData.setY(arena.bottomY + padding);
}

void ObjectEntity::scale(double value) {
    scaleFactor *= value;
    physicsData.setSize(physicsData.size * value);
    physicsData.setWidth(physicsData.width * value);

    if (isChild) {
        positionData.setX(positionData.x * value);
        positionData.setY(positionData.y * value);
    }
    for (ObjectEntity* entity : children) entity->scale(value);
}

void ObjectEntity::tick(int tick) {
    if (deletionAnimation) deletionAnimation->tick();

    for (size_t i = 0; i < children.size(); ++i) children[i]->tick(tick);

    if (isPhysical && !(physicsData.flags & PhysicsFlags::canEscapeArena)) keepInArena();
}
