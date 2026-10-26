#include "Native/UpcreateCompiler.h"
#include "Native/Camera.h"
#include "Native/Entity.h"
#include "Native/FieldGroups.h"
#include "Coder/Writer.h"
#include "Const/Enums.h"

#define D(g, bit) (((g)->dirty & (1u << (bit))) != 0)

static int swapColor(CameraEntity* camera, Entity* entity, StyleData* style, RelationsData* rel) {
    if (style->color != Color::Tank) return style->color;
    Entity* camPlayer = camera->getCameraData()->player;
    Entity* camTeam = (camPlayer && camPlayer->getRelationsData()) ? camPlayer->getRelationsData()->team : nullptr;
    bool sameTeam = rel && rel->team == camTeam;
    return sameTeam ? style->color : Color::EnemyTank;
}

void compileCreation(CameraEntity* camera, Writer& w, Entity* entity) {
    w.entid(entity).u8(1);

    RelationsData* rel = entity->getRelationsData();
    BarrelData* barrel = entity->getBarrelData();
    PhysicsData* phys = entity->getPhysicsData();
    HealthData* health = entity->getHealthData();
    ArenaData* arena = entity->getArenaData();
    NameData* name = entity->getNameData();
    CameraData* cam = entity->getCameraData();
    PositionData* pos = entity->getPositionData();
    StyleData* style = entity->getStyleData();
    ScoreData* score = entity->getScoreData();
    TeamData* team = entity->getTeamData();

    bool hasRelations = rel, hasBarrel = barrel, hasPhysics = phys, hasHealth = health;
    bool hasArena = arena, hasName = name, hasCamera = cam, hasPosition = pos;
    bool hasStyle = style, hasScore = score, hasTeam = team;

    int at = -1;
    if (hasRelations) { w.u8((0 - at) ^ 1); at = 0; }
    if (hasBarrel) { w.u8((2 - at) ^ 1); at = 2; }
    if (hasPhysics) { w.u8((3 - at) ^ 1); at = 3; }
    if (hasHealth) { w.u8((4 - at) ^ 1); at = 4; }
    if (hasArena) { w.u8((7 - at) ^ 1); at = 7; }
    if (hasName) { w.u8((8 - at) ^ 1); at = 8; }
    if (hasCamera) { w.u8((9 - at) ^ 1); at = 9; }
    if (hasPosition) { w.u8((10 - at) ^ 1); at = 10; }
    if (hasStyle) { w.u8((11 - at) ^ 1); at = 11; }
    if (hasScore) { w.u8((13 - at) ^ 1); at = 13; }
    if (hasTeam) { w.u8((14 - at) ^ 1); at = 14; }
    w.u8(1);

    if (hasPosition) w.vi((int32_t)pos->y);
    if (hasPosition) w.vi((int32_t)pos->x);
    if (hasPosition) w.float64Precision(pos->angle);
    if (hasPhysics) w.floatLE((float)phys->size);
    if (hasCamera) w.entid(cam->player);
    if (hasArena) w.vu((int32_t)arena->flags);
    if (hasStyle) w.vu(swapColor(camera, entity, style, rel));
    if (hasArena) for (at = 0; at < 10; ++at) w.vu((int32_t)arena->scoreboardColors.values[at]);
    if (hasCamera) w.stringNT(cam->killedBy);
    if (hasArena) w.vi((int32_t)arena->playersNeeded);
    if (hasPhysics) w.vu((int32_t)phys->sides);
    if (hasTeam) w.vu((int32_t)team->flags);
    if (hasHealth) w.vu((int32_t)health->flags);
    if (hasArena) for (at = 0; at < 10; ++at) w.vi((int32_t)arena->scoreboardTanks.values[at]);
    if (hasCamera) w.vi((int32_t)cam->respawnLevel);
    if (hasCamera) w.floatLE((float)cam->levelbarProgress);
    if (hasCamera) w.vi((int32_t)cam->spawnTick);
    if (hasPhysics) w.floatLE((float)phys->absorbtionFactor);
    if (hasArena) w.floatLE((float)arena->leaderX);
    if (hasHealth) w.floatLE((float)health->maxHealth);
    if (hasStyle) w.vu((int32_t)style->flags);
    if (hasBarrel) w.floatLE((float)barrel->trapezoidDirection);
    if (hasPosition) w.vu((int32_t)pos->flags);
    if (hasArena) for (at = 0; at < 10; ++at) w.stringNT(arena->scoreboardNames.values[at]);
    if (hasCamera) w.floatLE((float)cam->score);
    if (hasTeam) w.floatLE((float)team->mothershipY);
    if (hasArena) for (at = 0; at < 10; ++at) w.stringNT(arena->scoreboardSuffixes.values[at]);
    if (hasName) w.vu((int32_t)name->flags);
    if (hasCamera) w.floatLE((float)cam->movementSpeed);
    if (hasArena) w.floatLE((float)arena->leaderY);
    if (hasArena) w.floatLE((float)arena->bottomY);
    if (hasRelations) w.entid(rel->team);
    if (hasCamera) w.vi((int32_t)cam->level);
    if (hasTeam) w.vu((int32_t)team->teamColor);
    if (hasCamera) w.floatLE((float)cam->FOV);
    if (hasCamera) for (at = 0; at < 8; ++at) w.vi((int32_t)cam->statLimits.values[at]);
    if (hasArena) w.floatLE((float)arena->leftX);
    // floatLE here but vi in updates, same as upstream
    if (hasArena) for (at = 0; at < 10; ++at) w.floatLE((float)arena->scoreboardScores.values[at]);
    if (hasCamera) for (at = 0; at < 8; ++at) w.vi((int32_t)cam->statLevels.values[at]);
    if (hasCamera) w.stringNT(cam->tankOverride);
    if (hasCamera) w.vi((int32_t)cam->tank);
    if (hasStyle) w.float64Precision(style->borderWidth);
    if (hasCamera) w.vi((int32_t)cam->deathTick);
    if (hasPhysics) w.floatLE((float)phys->width);
    if (hasCamera) w.vi((int32_t)cam->statsAvailable);
    if (hasBarrel) w.vu((int32_t)barrel->flags);
    if (hasCamera) w.floatLE((float)cam->levelbarMax);
    if (hasName) w.stringNT(name->name);
    if (hasRelations) w.entid(rel->owner);
    if (hasHealth) w.floatLE((float)health->health);
    if (hasCamera) w.floatLE((float)cam->cameraY);
    if (hasStyle) w.floatLE((float)style->opacity);
    if (hasBarrel) w.floatLE((float)barrel->reloadTime);
    if (hasCamera) for (at = 0; at < 8; ++at) w.stringNT(cam->statNames.values[at]);
    if (hasCamera) w.floatLE((float)cam->cameraX);
    if (hasTeam) w.floatLE((float)team->mothershipX);
    if (hasCamera) w.vu((int32_t)cam->unusedClientId);
    if (hasRelations) w.entid(rel->parent);
    if (hasStyle) w.vu((int32_t)style->zIndex);
    if (hasCamera) w.vu((int32_t)cam->flags);
    if (hasArena) w.floatLE((float)arena->rightX);
    if (hasPhysics) w.floatLE((float)phys->pushFactor);
    if (hasPhysics) w.vu((int32_t)phys->flags);
    if (hasArena) w.vu((int32_t)arena->scoreboardAmount);
    if (hasArena) w.floatLE((float)arena->ticksUntilStart);
    if (hasArena) w.floatLE((float)arena->topY);
    if (hasScore) w.floatLE((float)score->score);
}

void compileUpdate(CameraEntity* camera, Writer& w, Entity* entity) {
    w.entid(entity).u8(0).u8(1);

    RelationsData* rel = entity->getRelationsData();
    BarrelData* barrel = entity->getBarrelData();
    PhysicsData* phys = entity->getPhysicsData();
    HealthData* health = entity->getHealthData();
    ArenaData* arena = entity->getArenaData();
    NameData* name = entity->getNameData();
    CameraData* cam = entity->getCameraData();
    PositionData* pos = entity->getPositionData();
    StyleData* style = entity->getStyleData();
    ScoreData* score = entity->getScoreData();
    TeamData* team = entity->getTeamData();

    bool hasRelations = rel, hasBarrel = barrel, hasPhysics = phys, hasHealth = health;
    bool hasArena = arena, hasName = name, hasCamera = cam, hasPosition = pos;
    bool hasStyle = style, hasScore = score, hasTeam = team;

    int at = -1;
    if (hasPosition && D(pos, 1)) { w.u8((0 - at) ^ 1); at = 0; w.vi((int32_t)pos->y); }
    if (hasPosition && D(pos, 0)) { w.u8((1 - at) ^ 1); at = 1; w.vi((int32_t)pos->x); }
    if (hasPosition && D(pos, 2)) { w.u8((2 - at) ^ 1); at = 2; w.float64Precision(pos->angle); }
    if (hasPhysics && D(phys, 2)) { w.u8((3 - at) ^ 1); at = 3; w.floatLE((float)phys->size); }
    if (hasCamera && D(cam, 2)) { w.u8((4 - at) ^ 1); at = 4; w.entid(cam->player); }
    if (hasArena && D(arena, 0)) { w.u8((5 - at) ^ 1); at = 5; w.vu((int32_t)arena->flags); }
    if (hasStyle && D(style, 1)) { w.u8((6 - at) ^ 1); at = 6; w.vu(swapColor(camera, entity, style, rel)); }
    if (hasArena && D(arena, 8)) {
        w.u8((7 - at) ^ 1); at = -1;
        for (int i = 0; i < 10; ++i) if (arena->scoreboardColors.dirty & (1u << i)) { w.u8((i - at) ^ 1); w.vu((int32_t)arena->scoreboardColors.values[i]); at = i; }
        w.u8(1); at = 7;
    }
    if (hasCamera && D(cam, 16)) { w.u8((8 - at) ^ 1); at = 8; w.stringNT(cam->killedBy); }
    if (hasArena && D(arena, 13)) { w.u8((9 - at) ^ 1); at = 9; w.vi((int32_t)arena->playersNeeded); }
    if (hasPhysics && D(phys, 1)) { w.u8((10 - at) ^ 1); at = 10; w.vu((int32_t)phys->sides); }
    if (hasTeam && D(team, 3)) { w.u8((11 - at) ^ 1); at = 11; w.vu((int32_t)team->flags); }
    if (hasHealth && D(health, 0)) { w.u8((12 - at) ^ 1); at = 12; w.vu((int32_t)health->flags); }
    if (hasArena && D(arena, 10)) {
        w.u8((13 - at) ^ 1); at = -1;
        for (int i = 0; i < 10; ++i) if (arena->scoreboardTanks.dirty & (1u << i)) { w.u8((i - at) ^ 1); w.vi((int32_t)arena->scoreboardTanks.values[i]); at = i; }
        w.u8(1); at = 13;
    }
    if (hasCamera && D(cam, 15)) { w.u8((14 - at) ^ 1); at = 14; w.vi((int32_t)cam->respawnLevel); }
    if (hasCamera && D(cam, 6)) { w.u8((15 - at) ^ 1); at = 15; w.floatLE((float)cam->levelbarProgress); }
    if (hasCamera && D(cam, 17)) { w.u8((16 - at) ^ 1); at = 16; w.vi((int32_t)cam->spawnTick); }
    if (hasPhysics && D(phys, 4)) { w.u8((17 - at) ^ 1); at = 17; w.floatLE((float)phys->absorbtionFactor); }
    if (hasArena && D(arena, 11)) { w.u8((18 - at) ^ 1); at = 18; w.floatLE((float)arena->leaderX); }
    if (hasHealth && D(health, 2)) { w.u8((19 - at) ^ 1); at = 19; w.floatLE((float)health->maxHealth); }
    if (hasStyle && D(style, 0)) { w.u8((20 - at) ^ 1); at = 20; w.vu((int32_t)style->flags); }
    if (hasBarrel && D(barrel, 2)) { w.u8((22 - at) ^ 1); at = 22; w.floatLE((float)barrel->trapezoidDirection); }
    if (hasPosition && D(pos, 3)) { w.u8((23 - at) ^ 1); at = 23; w.vu((int32_t)pos->flags); }
    if (hasArena && D(arena, 6)) {
        w.u8((24 - at) ^ 1); at = -1;
        for (int i = 0; i < 10; ++i) if (arena->scoreboardNames.dirty & (1u << i)) { w.u8((i - at) ^ 1); w.stringNT(arena->scoreboardNames.values[i]); at = i; }
        w.u8(1); at = 24;
    }
    if (hasCamera && D(cam, 14)) { w.u8((25 - at) ^ 1); at = 25; w.floatLE((float)cam->score); }
    if (hasTeam && D(team, 2)) { w.u8((26 - at) ^ 1); at = 26; w.floatLE((float)team->mothershipY); }
    if (hasArena && D(arena, 9)) {
        w.u8((27 - at) ^ 1); at = -1;
        for (int i = 0; i < 10; ++i) if (arena->scoreboardSuffixes.dirty & (1u << i)) { w.u8((i - at) ^ 1); w.stringNT(arena->scoreboardSuffixes.values[i]); at = i; }
        w.u8(1); at = 27;
    }
    if (hasName && D(name, 0)) { w.u8((28 - at) ^ 1); at = 28; w.vu((int32_t)name->flags); }
    if (hasCamera && D(cam, 20)) { w.u8((29 - at) ^ 1); at = 29; w.floatLE((float)cam->movementSpeed); }
    if (hasArena && D(arena, 12)) { w.u8((30 - at) ^ 1); at = 30; w.floatLE((float)arena->leaderY); }
    if (hasArena && D(arena, 4)) { w.u8((31 - at) ^ 1); at = 31; w.floatLE((float)arena->bottomY); }
    if (hasRelations && D(rel, 2)) { w.u8((32 - at) ^ 1); at = 32; w.entid(rel->team); }
    if (hasCamera && D(cam, 4)) { w.u8((33 - at) ^ 1); at = 33; w.vi((int32_t)cam->level); }
    if (hasTeam && D(team, 0)) { w.u8((34 - at) ^ 1); at = 34; w.vu((int32_t)team->teamColor); }
    if (hasCamera && D(cam, 3)) { w.u8((35 - at) ^ 1); at = 35; w.floatLE((float)cam->FOV); }
    if (hasCamera && D(cam, 11)) {
        w.u8((36 - at) ^ 1); at = -1;
        for (int i = 0; i < 8; ++i) if (cam->statLimits.dirty & (1u << i)) { w.u8((i - at) ^ 1); w.vi((int32_t)cam->statLimits.values[i]); at = i; }
        w.u8(1); at = 36;
    }
    if (hasArena && D(arena, 1)) { w.u8((37 - at) ^ 1); at = 37; w.floatLE((float)arena->leftX); }
    if (hasArena && D(arena, 7)) {
        w.u8((38 - at) ^ 1); at = -1;
        for (int i = 0; i < 10; ++i) if (arena->scoreboardScores.dirty & (1u << i)) { w.u8((i - at) ^ 1); w.vi((int32_t)arena->scoreboardScores.values[i]); at = i; }
        w.u8(1); at = 38;
    }
    if (hasCamera && D(cam, 10)) {
        w.u8((39 - at) ^ 1); at = -1;
        for (int i = 0; i < 8; ++i) if (cam->statLevels.dirty & (1u << i)) { w.u8((i - at) ^ 1); w.vi((int32_t)cam->statLevels.values[i]); at = i; }
        w.u8(1); at = 39;
    }
    if (hasCamera && D(cam, 19)) { w.u8((40 - at) ^ 1); at = 40; w.stringNT(cam->tankOverride); }
    if (hasCamera && D(cam, 5)) { w.u8((41 - at) ^ 1); at = 41; w.vi((int32_t)cam->tank); }
    if (hasStyle && D(style, 2)) { w.u8((42 - at) ^ 1); at = 42; w.float64Precision(style->borderWidth); }
    if (hasCamera && D(cam, 18)) { w.u8((43 - at) ^ 1); at = 43; w.vi((int32_t)cam->deathTick); }
    if (hasPhysics && D(phys, 3)) { w.u8((44 - at) ^ 1); at = 44; w.floatLE((float)phys->width); }
    if (hasCamera && D(cam, 8)) { w.u8((45 - at) ^ 1); at = 45; w.vi((int32_t)cam->statsAvailable); }
    if (hasBarrel && D(barrel, 0)) { w.u8((46 - at) ^ 1); at = 46; w.vu((int32_t)barrel->flags); }
    if (hasCamera && D(cam, 7)) { w.u8((47 - at) ^ 1); at = 47; w.floatLE((float)cam->levelbarMax); }
    if (hasName && D(name, 1)) { w.u8((48 - at) ^ 1); at = 48; w.stringNT(name->name); }
    if (hasRelations && D(rel, 1)) { w.u8((49 - at) ^ 1); at = 49; w.entid(rel->owner); }
    if (hasHealth && D(health, 1)) { w.u8((50 - at) ^ 1); at = 50; w.floatLE((float)health->health); }
    if (hasCamera && D(cam, 13)) { w.u8((51 - at) ^ 1); at = 51; w.floatLE((float)cam->cameraY); }
    if (hasStyle && D(style, 3)) { w.u8((52 - at) ^ 1); at = 52; w.floatLE((float)style->opacity); }
    if (hasBarrel && D(barrel, 1)) { w.u8((53 - at) ^ 1); at = 53; w.floatLE((float)barrel->reloadTime); }
    if (hasCamera && D(cam, 9)) {
        w.u8((54 - at) ^ 1); at = -1;
        for (int i = 0; i < 8; ++i) if (cam->statNames.dirty & (1u << i)) { w.u8((i - at) ^ 1); w.stringNT(cam->statNames.values[i]); at = i; }
        w.u8(1); at = 54;
    }
    if (hasCamera && D(cam, 12)) { w.u8((55 - at) ^ 1); at = 55; w.floatLE((float)cam->cameraX); }
    if (hasTeam && D(team, 1)) { w.u8((56 - at) ^ 1); at = 56; w.floatLE((float)team->mothershipX); }
    if (hasCamera && D(cam, 0)) { w.u8((57 - at) ^ 1); at = 57; w.vu((int32_t)cam->unusedClientId); }
    if (hasRelations && D(rel, 0)) { w.u8((58 - at) ^ 1); at = 58; w.entid(rel->parent); }
    if (hasStyle && D(style, 4)) { w.u8((59 - at) ^ 1); at = 59; w.vu((int32_t)style->zIndex); }
    if (hasCamera && D(cam, 1)) { w.u8((60 - at) ^ 1); at = 60; w.vu((int32_t)cam->flags); }
    if (hasArena && D(arena, 3)) { w.u8((61 - at) ^ 1); at = 61; w.floatLE((float)arena->rightX); }
    if (hasPhysics && D(phys, 5)) { w.u8((62 - at) ^ 1); at = 62; w.floatLE((float)phys->pushFactor); }
    if (hasPhysics && D(phys, 0)) { w.u8((63 - at) ^ 1); at = 63; w.vu((int32_t)phys->flags); }
    if (hasArena && D(arena, 5)) { w.u8((64 - at) ^ 1); at = 64; w.vu((int32_t)arena->scoreboardAmount); }
    if (hasArena && D(arena, 14)) { w.u8((65 - at) ^ 1); at = 65; w.floatLE((float)arena->ticksUntilStart); }
    if (hasArena && D(arena, 2)) { w.u8((66 - at) ^ 1); at = 66; w.floatLE((float)arena->topY); }
    if (hasScore && D(score, 0)) { w.u8((67 - at) ^ 1); at = 67; w.floatLE((float)score->score); }

    w.u8(1);
}

#undef D
