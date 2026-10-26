#pragma once

#include <string>
#include <unordered_map>

#include "Native/Entity.h"
#include "Native/FieldGroups.h"
#include "Const/Enums.h"

class ObjectEntity;
class TeamBase;

// Color int -> team display name
extern const std::unordered_map<int, std::string> ColorsTeamName;

class TeamEntity : public Entity {
public:
    TeamData teamData;
    std::string teamName;
    TeamBase* base = nullptr;
    // Random bytes identifying the team in party links.
    std::string partyCode;

    TeamEntity(GameServer* game, int color, const std::string& name = "");

    TeamData* getTeamData() override { return &teamData; }

    static bool isTeam(Entity* e) { return e && e->getTeamData() != nullptr; }
    // True only for actual TeamEntity instances; arena/motherships are team-like but not TeamEntity.
    static bool isTeamEntity(Entity* e) { return e && (e->entityTags & EntityTags::isTeamGroup); }
    static void setTeam(Entity* team, Entity* entity);
};
