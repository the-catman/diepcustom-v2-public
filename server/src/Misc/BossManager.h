#pragma once

#include "Native/Ref.h"

class ArenaEntity;
class AbstractBoss;

class BossManager {
public:
    explicit BossManager(ArenaEntity* arena);
    void tick(int tick);

private:
    ArenaEntity* arena;
    Ref<AbstractBoss> boss;

    void spawnBoss();
};
