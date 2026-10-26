#pragma once

#include <cstdint>
#include <vector>

class PackedEntitySet {
public:
    static constexpr int MAX_ENTITY_COUNT = 16384;
    static constexpr int SET_WORD_COUNT = MAX_ENTITY_COUNT / 32;

    std::vector<uint32_t> data;

    PackedEntitySet() : data(SET_WORD_COUNT, 0) {}

    void add(int entityId) {
        data[entityId >> 5] |= (1u << (entityId & 31));
    }
    void clear() {
        std::fill(data.begin(), data.end(), 0u);
    }

    static PackedEntitySet& fullSet() {
        static PackedEntitySet set = [] {
            PackedEntitySet s;
            std::fill(s.data.begin(), s.data.end(), 0xffffffffu);
            return s;
        }();
        return set;
    }
};
