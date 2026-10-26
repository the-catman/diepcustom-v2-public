#pragma once

#include <functional>

class ObjectEntity;
class PackedEntitySet;

class CollisionManager {
public:
    virtual ~CollisionManager() = default;

    virtual void preTick(int tick) = 0;
    virtual void postTick(int tick) = 0;

    virtual void insert(ObjectEntity* entity) = 0;
    virtual ObjectEntity* getFirstMatch(double centerX, double centerY, double halfWidth, double halfHeight,
                                        const std::function<bool(ObjectEntity*)>& predicate) = 0;
    virtual PackedEntitySet& retrieve(double centerX, double centerY, double halfWidth, double halfHeight) = 0;
    virtual void forEachCollisionPair(const std::function<void(ObjectEntity*, ObjectEntity*)>& callback) = 0;
};
