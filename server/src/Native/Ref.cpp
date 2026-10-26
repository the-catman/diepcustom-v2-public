#include "Native/Ref.h"
#include "Native/Entity.h"
#include "Native/Manager.h"
#include "Game.h"

void assignRef(Entity* e, Entity* const*& slots, int& id, uint32_t& hash) {
    if (e) {
        slots = e->game->entities->inner.data();
        id = e->id;
        hash = e->preservedHash;
    } else {
        slots = nullptr;
        id = -1;
        hash = 0;
    }
}
