#include "Achievements.h"

#include <cstdint>
#include <cstdio>
#include <emscripten/emscripten.h>

EM_JS(int, ach_load, (const char* key), {
    return parseInt(window.localStorage[UTF8ToString(key)] || "0", 10) | 0;
});

EM_JS(void, ach_save, (const char* key, int value), {
    window.localStorage[UTF8ToString(key)] = String(value);
});

namespace {
    struct Def {
        const char* name;
        const char* desc;
        int threshold;
        int latency; // "latency" condition: value >= this many ms; 0 = server event
    };

    // Generated from server/data/Achievements.json (all single-condition).
    const Def DEFS[] = {
        {"A moment to cherish forever", "Destroy your first tank", 1, 0},
        {"Git gud", "Destroy 10 tanks", 10, 0},
        {"Gitting gud", "Destroy 100 tanks", 100, 0},
        {"Got gud", "Destroy 1000 tanks", 1000, 0},
        {"Starting out", "Destroy something", 1, 0},
        {"Getting good", "Destroy 500 things", 500, 0},
        {"Destroy EVERYTHING", "Destroy 10000 things", 10000, 0},
        {"Gotta start somewhere", "Destroy 10 squares", 10, 0},
        {"Square hater", "Destroy 500 squares", 500, 0},
        {"These squares gotta go", "Destroy 10000 squares", 10000, 0},
        {"It hurts when I do this", "Ram into something", 1, 0},
        {"Who needs bullets anyway", "Ram into 100 things", 100, 0},
        {"Look mom, no cannons!", "Ram into 3000 things", 3000, 0},
        {"They ain't a real tank", "Destroy 100 factory drones", 100, 0},
        {"2fast4u", "Upgrade Movement Speed to its maximum value", 1, 0},
        {"Ratatatatatatatata", "Upgrade Reload to its maximum value", 1, 0},
        {"More dangerous than it looks", "Upgrade Bullet Damage to its maximum value", 1, 0},
        {"There's no stopping it!", "Upgrade Bullet Penetration to its maximum value", 1, 0},
        {"Mach 4", "Upgrade Bullet Speed to its maximum value", 1, 0},
        {"Don't touch me", "Upgrade Body Damage to its maximum value", 1, 0},
        {"Indestructible", "Upgrade Max Health to its maximum value", 1, 0},
        {"Self-repairing", "Upgrade Health Regen to its maximum value", 1, 0},
        {"Fire power", "Upgrade to Twin", 1, 0},
        {"Eat those bullets", "Upgrade to Machine Gun", 1, 0},
        {"Snipin'", "Upgrade to Sniper", 1, 0},
        {"Ain't no one sneaking up on ME", "Upgrade to Flank Guard", 1, 0},
        {"Three at the same time", "Upgrade to Triple Shot", 1, 0},
        {"I've got places to be", "Upgrade to Tri-Angle", 1, 0},
        {"BOOM, you're dead", "Upgrade to Destroyer", 1, 0},
        {"Drones are love", "Upgrade to Overseer", 1, 0},
        {"C + E", "Upgrade to Quad Tank", 1, 0},
        {"Now you really ain't sneaking up on me", "Upgrade to Twin Flank", 1, 0},
        {"Insert uncreative achievement name here", "Upgrade to Assassin", 1, 0},
        {"Huntin'", "Upgrade to Hunter", 1, 0},
        {"Eat those pellets!", "Upgrade to Gunner", 1, 0},
        {"BUILD A WALL", "Upgrade to Trapper", 1, 0},
        {"Can't bother using both hands to play?", "Upgrade to Auto 3", 1, 0},
        {"Where did my cannon go?", "Upgrade to Smasher", 1, 0},
        {"Try some other classes too", "Upgrade to Sniper 100 times", 100, 0},
        {"Drones are life", "Upgrade to Overseer 100 times", 100, 0},
        {"That was tough", "Kill a boss", 1, 0},
        {"Boss Hunter", "Kill 10 bosses", 10, 0},
        {"Eh, you're trying", "Reach 1k points", 1, 0},
        {"Starting to git gud", "Reach 10k points", 1, 0},
        {"You aren't that bad at this", "Reach 100k points", 1, 0},
        {"Okay you're pretty good", "Reach 1m points", 1, 0},
        {"Jackpot!", "Get 20k points in a single kill", 1, 0},
        {"LAAAAAAAAAAAAG", "Play with over 1000 ms of latency", 1, 1000},
        {"Shiny!", "???", 1, 0},
        {"There are other classes?", "Get to level 45 as a basic tank", 1, 0},
    };
    constexpr int DEF_COUNT = sizeof(DEFS) / sizeof(DEFS[0]);

    struct Counter {
        std::string key;     // the condition key the server sends
        std::string storage; // "A::" + key
        int value = 0;
    };
    std::vector<Counter> counters;

    // MurmurHash2 of names (seed 170) and descriptions (seed 221).
    uint32_t murmur2(const char* s, uint32_t seed) {
        const uint32_t m = 0x5bd1e995;
        const auto* p = (const uint8_t*)s;
        size_t len = 0;
        while (s[len]) ++len;
        uint32_t h = seed ^ (uint32_t)len;
        while (len >= 4) {
            uint32_t k = p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24);
            k *= m;
            k ^= k >> 24;
            k *= m;
            h = (h * m) ^ k;
            p += 4;
            len -= 4;
        }
        switch (len) {
            case 3: h ^= p[2] << 16; [[fallthrough]];
            case 2: h ^= p[1] << 8; [[fallthrough]];
            case 1: h ^= p[0]; h *= m;
        }
        h ^= h >> 13;
        h *= m;
        h ^= h >> 15;
        return h;
    }

    void init() {
        if (!counters.empty()) return;
        counters.resize(DEF_COUNT);
        for (int i = 0; i < DEF_COUNT; ++i) {
            char key[24];
            // The root "and" of `conds` is _0; its single condition is _1.
            std::snprintf(key, sizeof(key), "%x%x_1", murmur2(DEFS[i].name, 170), murmur2(DEFS[i].desc, 221));
            counters[i].key = key;
            counters[i].storage = std::string("A::") + key;
            counters[i].value = ach_load(counters[i].storage.c_str());
        }
    }

    void advance(int i) {
        if (achievements::unlocked(i)) return;
        Counter& c = counters[i];
        ach_save(c.storage.c_str(), ++c.value);
    }
}

namespace achievements {
    int count() { return DEF_COUNT; }
    const char* name(int i) { return DEFS[i].name; }
    const char* desc(int i) { return DEFS[i].desc; }

    bool unlocked(int i) {
        init();
        return counters[i].value >= DEFS[i].threshold;
    }

    bool hasProgress(int i) { return DEFS[i].threshold > 1; }

    double progress(int i) {
        init();
        double p = (double)counters[i].value / DEFS[i].threshold;
        return p < 0 ? 0 : p > 1 ? 1 : p;
    }

    void onKeys(const std::vector<std::string>& keys) {
        init();
        for (const std::string& key : keys) {
            for (int i = 0; i < DEF_COUNT; ++i) {
                if (counters[i].key == key) { advance(i); break; }
            }
        }
    }

    void onLatency(double ms) {
        init();
        for (int i = 0; i < DEF_COUNT; ++i) {
            if (DEFS[i].latency > 0 && ms >= DEFS[i].latency) advance(i);
        }
    }
}
