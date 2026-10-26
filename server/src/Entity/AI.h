#pragma once

#include <functional>
#include <memory>

#include "Physics/Vector.h"
#include "Const/Enums.h"
#include "Native/Ref.h"

class GameServer;
class ObjectEntity;

enum class AIState { idle = 0, hasTarget = 1, possessed = 3 };

// Shared input surface for AIs and Clients; either can replace the other.
class Inputs {
public:
    uint32_t flags = 0;
    Vector mouse;
    Vector movement;
    bool deleted = false;

    Inputs() {}
    virtual ~Inputs() = default;

    bool attemptingShot() const { return flags & InputFlags::leftclick; }
    bool attemptingRepel() const { return flags & InputFlags::rightclick; }
};

// The intelligence behind auto turrets.
class AI {
public:
    static constexpr double PASSIVE_ROTATION = 0.01;
    bool isClaimable = false;

    double passiveRotation;
    double viewRange = 1700;
    AIState state = AIState::idle;

    std::shared_ptr<Inputs> inputs;
    Ref<ObjectEntity> owner;
    GameServer* game;
    Ref<ObjectEntity> target;
    double movementSpeed = 1;
    double aimSpeed = 1;
    bool doAimPrediction = false;

    bool targetFilterNonLiving = true;
    bool targetArenaTeam = true;
    std::function<bool(const VectorAbstract&)> targetFilter;

    AI(ObjectEntity* owner, bool claimable = false);
    ~AI();

    ObjectEntity* findTarget(int tick);
    void aimAtTarget();
    void tick(int tick);
    void setFindTargetInterval(int v) { _findTargetInterval = v; }

private:
    int _aiHash;
    static int _aiHashCounter;
    int _findTargetInterval = 2;
};
