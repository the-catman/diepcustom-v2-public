#include "Native/Entity.h"
#include "Native/FieldGroups.h"
#include "Native/Manager.h"
#include "Game.h"

Entity::Entity(GameServer* game, EntityCategory category) : game(game), category(category) {
    game->entities->add(this);
}

void Entity::wipeState() {
    if (auto* g = getRelationsData()) g->wipe();
    if (auto* g = getBarrelData()) g->wipe();
    if (auto* g = getPhysicsData()) g->wipe();
    if (auto* g = getHealthData()) g->wipe();
    if (auto* g = getArenaData()) g->wipe();
    if (auto* g = getNameData()) g->wipe();
    if (auto* g = getCameraData()) g->wipe();
    if (auto* g = getPositionData()) g->wipe();
    if (auto* g = getStyleData()) g->wipe();
    if (auto* g = getScoreData()) g->wipe();
    if (auto* g = getTeamData()) g->wipe();
    entityState = 0;
}

void Entity::delete_() {
    wipeState();
    game->entities->delete_(id);
}
