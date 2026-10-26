#include "Entity/Misc/Mothership.h"
#include "Entity/Misc/TeamEntity.h"
#include "Native/Arena.h"
#include "Native/Camera.h"
#include "Client.h"
#include "Game.h"
#include "Const/Enums.h"
#include "Config.h"

#include <cmath>

static constexpr double POSSESSION_TIMER = config::tps * 60 * 5;

Mothership::Mothership(GameServer* game)
    : TankBody(game, new CameraEntity(game), std::make_shared<Inputs>()) {
    arenaMobID = "Mothership";
    relationsData.setTeam(game->arena);
    styleData.setColor(Color::Neutral);

    ai = new AI(this, true);
    ai->inputs = inputs;
    ai->viewRange = 2000;

    cameraEntity->cameraData.setPlayer(this);
    setTank(Tank::Mothership);
    cameraEntity->setLevel(140);
    nameData.setName("Mothership");

    scoreReward = 0;

    for (int i = Stat::MovementSpeed; i < Stat::HealthRegen; ++i) cameraEntity->setStat(i, 7);
    cameraEntity->setStat(Stat::HealthRegen, 1);

    healthData.health = 7000;
    healthData.maxHealth = 7000;
}

void Mothership::onDeath(LivingEntity* killer) {
    if ((int)game->arena->state >= (int)ArenaState::OVER) return;

    Entity* team = relationsData.team;
    bool teamIsATeam = TeamEntity::isTeam(team);

    Entity* killerTeam = killer->relationsData.team;
    bool killerTeamIsATeam = TeamEntity::isTeam(killerTeam);

    std::string killerName;
    if (killerTeamIsATeam) {
        TeamEntity* kte = TeamEntity::isTeamEntity(killerTeam) ? static_cast<TeamEntity*>(killerTeam) : nullptr;
        killerName = (kte && !kte->teamName.empty()) ? kte->teamName : "a mysterious group";
    } else {
        NameData* nd = killer->getNameData();
        killerName = (nd && !nd->name.empty()) ? nd->name : "an unnamed tank";
    }

    std::string teamName;
    if (teamIsATeam) {
        TeamEntity* te = TeamEntity::isTeamEntity(team) ? static_cast<TeamEntity*>(team) : nullptr;
        teamName = ((te && !te->teamName.empty()) ? te->teamName : "a mysterious group") + std::string("'s");
    } else {
        teamName = "a";
    }

    TeamData* ktd = killerTeamIsATeam ? killerTeam->getTeamData() : nullptr;
    uint32_t color = ktd ? colorHex(ktd->teamColor) : 0x000000;

    game->broadcast()
        .u8(ClientBound::Notification)
        .stringNT(killerName + " has destroyed " + teamName + " Mothership!")
        .u32(color)
        .floatLE(-1)
        .stringNT("").send();
}

void Mothership::delete_() {
    TeamData* td = relationsData.team ? relationsData.team->getTeamData() : nullptr;
    if (td) td->setFlags(td->flags & ~TeamFlags::hasMothership);
    TankBody::delete_();
}

void Mothership::tick(int tick) {
    if (barrels.empty()) { TankBody::tick(tick); return; }

    inputs = ai->inputs;

    if (ai->state == AIState::idle) {
        double angle = positionData.angle + ai->passiveRotation;
        double dx = inputs->mouse.x - positionData.x;
        double dy = inputs->mouse.y - positionData.y;
        double mag = std::sqrt(dx * dx + dy * dy);
        inputs->mouse.x = positionData.x + std::cos(angle) * mag;
        inputs->mouse.y = positionData.y + std::sin(angle) * mag;
    } else if (ai->state == AIState::possessed && possessionStartTick == -1) {
        possessionStartTick = tick;
    }
    if (possessionStartTick != -1 && ai->state != AIState::possessed) possessionStartTick = -1;

    if (possessionStartTick != -1) {
        if (possessionStartTick != -1 && ai->state != AIState::possessed) {
            possessionStartTick = -1;
        } else {
            ClientInputs* ci = dynamic_cast<ClientInputs*>(inputs.get());
            if (ci && ci->client) {
                if (tick - possessionStartTick >= (int)POSSESSION_TIMER) {
                    inputs->deleted = true;
                } else if (tick - possessionStartTick == (int)(POSSESSION_TIMER - 10 * config::tps)) {
                    ci->client->notify("You only have 10 seconds left in control of the Mothership",
                        colorHex(styleData.color), 5000, "");
                }
            }
        }
    }

    Entity* team = relationsData.team;
    TeamData* td = team ? team->getTeamData() : nullptr;
    if (td) {
        td->setMothershipX(positionData.x);
        td->setMothershipY(positionData.y);
        td->setFlags(td->flags | TeamFlags::hasMothership);
    }

    TankBody::tick(tick);
}
