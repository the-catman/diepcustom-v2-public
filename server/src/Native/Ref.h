#pragma once

#include <cstdint>

#include "Native/Entity.h"

// Points slots at the manager's entity table (fixed size, so the pointer stays valid).
void assignRef(Entity* e, Entity* const*& slots, int& id, uint32_t& hash);

// Non-owning {id, hash} handle; resolves via the manager so freed slots read as nullptr.
template <class T>
class Ref {
public:
    Ref() = default;
    Ref(decltype(nullptr)) {}
    Ref(T* e) { assignRef(e, slots, id, h); }

    Ref& operator=(T* e) { assignRef(e, slots, id, h); return *this; }
    Ref& operator=(decltype(nullptr)) { slots = nullptr; id = -1; h = 0; return *this; }

    // Matches preservedHash so a deleted-but-not-yet-reclaimed entity still resolves this tick.
    // Entity is always the offset-0 base, so the cast is address-preserving.
    T* get() const {
        if (!slots || h == 0) return nullptr;
        Entity* e = slots[id];
        return (e && e->preservedHash == h) ? reinterpret_cast<T*>(e) : nullptr;
    }
    operator T*() const { return get(); }
    T* operator->() const { return get(); }
    explicit operator bool() const { return get() != nullptr; }

    // Stored identity, valid even after the target is freed (used for delete packets).
    int rawId() const { return id; }
    uint32_t rawHash() const { return h; }

    // Identity compare that survives deletion (pointer compare would collapse to null == null).
    template <class U>
    bool sameAs(const Ref<U>& o) const { return id == o.rawId() && h == o.rawHash(); }

private:
    Entity* const* slots = nullptr;
    int id = -1;
    uint32_t h = 0;
};
