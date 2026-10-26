#pragma once

#include <memory>
#include <string>
#include <vector>

#include "Native/Entity.h"
#include "Native/FieldGroups.h"
#include "Physics/Vector.h"
#include "Entity/AI.h"

class ObjectEntity;
class CameraEntity;
class LivingEntity;

class DeletionAnimation {
public:
    int frame = 5;
    explicit DeletionAnimation(ObjectEntity* entity) : entity(entity) {}
    void tick();
private:
    ObjectEntity* entity;
};

// Everything visible in game: relations, physics, position, style.
class ObjectEntity : public Entity {
public:
    RelationsData relationsData;
    PhysicsData physicsData;
    PositionData positionData;
    StyleData styleData;

    DeletionAnimation* deletionAnimation = nullptr;
    bool isPhysical = true;
    bool isChild = false;
    std::vector<ObjectEntity*> children;
    ObjectEntity* rootParent = this;
    double scaleFactor = 1;
    std::string arenaMobID = "";
    Vector velocity;

    // Carried here so barrel-owning entities need no separate base (no diamond).
    Ref<CameraEntity> cameraEntity;
    double reloadTime = 15;
    std::shared_ptr<Inputs> inputs;

    explicit ObjectEntity(GameServer* game);
    ~ObjectEntity() override { delete deletionAnimation; }

    RelationsData* getRelationsData() override { return &relationsData; }
    PhysicsData* getPhysicsData() override { return &physicsData; }
    PositionData* getPositionData() override { return &positionData; }
    StyleData* getStyleData() override { return &styleData; }

    static bool isObject(Entity* entity) { return entity && entity->getPhysicsData() != nullptr; }
    static void handleCollision(ObjectEntity* a, ObjectEntity* b);
    static bool isColliding(ObjectEntity* a, ObjectEntity* b);

    virtual void destroy(bool animate = true);
    void delete_() override;

    // Virtual so kill credit passes through non-living owners (AutoTurret, GuardObject).
    virtual void onKill(LivingEntity* killedEntity, LivingEntity* weapon) {}

    void addVelocity(double angle, double magnitude);
    void setVelocity(double angle, double magnitude);
    void maintainVelocity(double angle, double maxSpeed);
    virtual void applyPhysics();
    void setParent(ObjectEntity* parent);
    Vector getWorldPosition();
    void setGlobalEntity();
    void scale(double value);
    void tick(int tick) override;

protected:
    void receiveKnockback(ObjectEntity* entity);
    void keepInArena();
};


using BarrelBase = ObjectEntity;
