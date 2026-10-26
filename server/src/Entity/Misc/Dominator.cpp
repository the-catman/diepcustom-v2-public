#include "Entity/Misc/Dominator.h"
#include "Entity/Misc/TeamBase.h"
#include "Entity/Misc/TeamEntity.h"
#include "Entity/Tank/Barrel.h"
#include "Native/Arena.h"
#include "Native/Manager.h"
#include "Native/Camera.h"
#include "Client.h"
#include "Game.h"
#include "Util.h"
#include "Const/Enums.h"

#include <cmath>
#include <algorithm>

const std::vector<int> Dominator::DOMINATOR_CLASSES = { Tank::DominatorD, Tank::DominatorG, Tank::DominatorT };

Dominator::Dominator(ArenaEntity* arena, TeamBase* base, int tankId)
    : TankBody(arena->game, new CameraEntity(arena->game), std::make_shared<Inputs>()) {
    if (tankId == -1) tankId = util::randomFrom(DOMINATOR_CLASSES);

    cameraEntity->cameraData.setPlayer(this);
    cameraEntity->setLevel(75);

    scaleFactor = 1;
    scale(SIZE / baseSize);

    setTank(tankId);

    relationsData.setTeam(arena);
    physicsData.size = SIZE;
    styleData.setColor(Color::Neutral);

    ai = new AI(this, true);
    ai->inputs = inputs;
    ai->movementSpeed = 0;
    ai->viewRange = 2000;
    ai->doAimPrediction = true;

    if (definition) {
        localDef = *definition;
        localDef.speed = 0;
        definition = &localDef;
    }
    cameraEntity->cameraData.movementSpeed = 0;

    nameData.setName("Dominator");
    nameData.setFlags(nameData.flags | NameFlags::hiddenName);
    physicsData.absorbtionFactor = 0;

    positionData.x = base->positionData.x;
    positionData.y = base->positionData.y;

    scoreReward = 0;

    this->base = base;
    damagePerTick = 10;

    if (styleData.flags & StyleFlags::isFlashing) {
        styleData.flags ^= StyleFlags::isFlashing;
        damageReduction = 1.0;
    }

    entityTags |= EntityTags::isDominator;
}

void Dominator::onDeath(LivingEntity* killer) {
    Entity* killerTeam = killer->relationsData.team;
    bool killerTeamIsATeam = TeamEntity::isTeam(killerTeam);

    if (killerTeamIsATeam && relationsData.team == (Entity*)game->arena) {
        // capture neutral dominator
        relationsData.setTeam(killerTeam);
        TeamData* td = killerTeam->getTeamData();
        styleData.setColor(td ? td->teamColor : Color::Neutral);

        TeamEntity* te = TeamEntity::isTeamEntity(killerTeam) ? static_cast<TeamEntity*>(killerTeam) : nullptr;
        game->broadcast()
            .u8(ClientBound::Notification)
            .stringNT("The " + prefix + nameData.name + " is now controlled by " + ((te && !te->teamName.empty()) ? te->teamName : "a mysterious group"))
            .u32(td ? colorHex(td->teamColor) : 0x000000)
            .floatLE(7500)
            .stringNT("").send();

        for (Client* client : game->clients) {
            if (!client->camera) continue;
            if (client->camera->relationsData.team == relationsData.team)
                client->notify("Press H to take control of the " + nameData.name, td ? colorHex(td->teamColor) : 0x000000);
        }
    } else {
        // set to neutral team
        relationsData.setTeam(game->arena);
        styleData.setColor(game->arena->teamData.teamColor);

        game->broadcast()
            .u8(ClientBound::Notification)
            .stringNT("The " + prefix + nameData.name + " is being contested")
            .u32(colorHex(Color::Neutral))
            .floatLE(7500)
            .stringNT("").send();
    }

    base->styleData.setColor(styleData.color);
    base->relationsData.setTeam(relationsData.team);

    healthData.setHealth(healthData.maxHealth);

    for (int i = 1; i <= game->entities->lastId; ++i) {
        Entity* entity = game->entities->inner[i];
        if (!Entity::exists(entity)) continue;
        RelationsData* rd = entity->getRelationsData();
        if ((entity->entityTags & EntityTags::isBullet) && rd && rd->owner == this)
            static_cast<ObjectEntity*>(entity)->destroy();
    }

    if (ai->state == AIState::possessed) {
        ai->inputs->deleted = true;
        ai->inputs = inputs = std::make_shared<Inputs>();
        ai->state = AIState::idle;
    }
}

void Dominator::tick(int tick) {
    if (barrels.empty()) { TankBody::tick(tick); return; }

    ai->aimSpeed = barrels[0]->bulletAccel;
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
