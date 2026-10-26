#include "Entity/Misc/TeamEntity.h"
#include "Entity/Object.h"
#include "Entity/Tank/TankBody.h"
#include "Native/Camera.h"
#include "Const/Enums.h"
#include "Util.h"

const std::unordered_map<int, std::string> ColorsTeamName = {
    { Color::Border,          "BORDER"     },
    { Color::Barrel,          "BARREL"     },
    { Color::Tank,            "TANK"       },
    { Color::TeamBlue,        "BLUE"       },
    { Color::TeamRed,         "RED"        },
    { Color::TeamPurple,      "PURPLE"     },
    { Color::TeamGreen,       "GREEN"      },
    { Color::Shiny,           "SHINY"      },
    { Color::EnemySquare,     "SQUARE"     },
    { Color::EnemyTriangle,   "TRIANGLE"   },
    { Color::EnemyPentagon,   "PENTAGON"   },
    { Color::EnemyCrasher,    "CRASHER"    },
    { Color::Neutral,         "NEUTRAL"    },
    { Color::ScoreboardBar,   "SCOREBOARD" },
    { Color::Box,             "MAZE"       },
    { Color::EnemyTank,       "ENEMY"      },
    { Color::NecromancerSquare, "SUNCHIP"  },
    { Color::Fallen,          "FALLEN"     },
};

TeamEntity::TeamEntity(GameServer* game, int color, const std::string& name)
    : Entity(game), teamData(this) {
    entityTags |= EntityTags::isTeamGroup;
    teamData.teamColor = color;
    auto it = ColorsTeamName.find(color);
    teamName = name.empty() ? (it != ColorsTeamName.end() ? it->second : "UNKNOWN") : name;
    for (int i = 0; i < 8; ++i) partyCode += (char)(int)(util::randomFloat() * 256);
}

void TeamEntity::setTeam(Entity* team, Entity* entity) {
    if (!Entity::exists(entity)) return;
    if (!ObjectEntity::isObject(entity)) return;
    ObjectEntity* obj = static_cast<ObjectEntity*>(entity);

    obj->relationsData.team = team;

    TeamData* td = team ? team->getTeamData() : nullptr;
    if (td) obj->styleData.setColor(td->teamColor);

    if (TankBody::isTank(entity)) {
        TankBody* tank = static_cast<TankBody*>(entity);
        if (tank->cameraEntity) tank->cameraEntity->relationsData.team = team;
    }
}
