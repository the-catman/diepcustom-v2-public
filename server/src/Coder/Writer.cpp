#include "Coder/Writer.h"
#include "Native/Entity.h"

Writer& Writer::entid(Entity* entity) {
    if (!entity || entity->hash == 0) return u8(0);
    return vu((int32_t)entity->hash).vu((int32_t)entity->id);
}
