#include "Const/Achievements.h"

#include "Client.h"
#include "Config.h"
#include "Const/Enums.h"
#include "Game.h"
#include "Native/Arena.h"

#include "nlohmann/json.hpp"

#include <cstdio>
#include <fstream>
#include <vector>

using nlohmann::json;
using achievements::Tag;

namespace {
    struct CondTag {
        enum Op : uint8_t { Equals, AtLeast, AtMost };

        std::string key;
        Tag::Kind kind;
        Op op = Equals;
        double number = 0;
        std::string string;
    };

    struct Condition {
        std::string event;
        std::vector<CondTag> tags;
        std::string key;
    };

    std::vector<Condition> conditions;

    // MurmurHash2 of names (seed 170) and descriptions (seed 221), as the client hashes them.
    uint32_t murmur2(const std::string& s, uint32_t seed) {
        const uint32_t m = 0x5bd1e995;
        const auto* p = (const uint8_t*)s.data();
        size_t len = s.size();
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

    CondTag parseTag(const std::string& key, const json& value) {
        CondTag t{key, Tag::String};
        if (value.is_boolean()) {
            t.kind = Tag::Bool;
            t.number = value.get<bool>();
        } else if (value.is_number()) {
            t.kind = Tag::Number;
            t.number = value.get<double>();
        } else {
            std::string s = value.get<std::string>();
            std::string op = s.substr(0, 2);
            if (op == ">=" || op == "<=" || op == "==") {
                t.kind = Tag::Number;
                t.op = op == ">=" ? CondTag::AtLeast : op == "<=" ? CondTag::AtMost : CondTag::Equals;
                t.number = std::stod(s.substr(2));
            } else {
                t.string = s;
            }
        }
        return t;
    }

    bool matches(const CondTag& want, const Tag& got) {
        if (want.kind != got.kind) return false;
        if (want.kind == Tag::String) return want.string == got.string;
        switch (want.op) {
            case CondTag::AtLeast: return got.number >= want.number;
            case CondTag::AtMost: return got.number <= want.number;
            default: return got.number == want.number;
        }
    }
}

namespace achievements {
    void init() {
        std::ifstream f("data/Achievements.json");
        json arr;
        f >> arr;

        conditions.clear();
        for (auto& a : arr) {
            char base[20];
            std::snprintf(base, sizeof(base), "%x%x",
                          murmur2(a.at("name").get<std::string>(), 170),
                          murmur2(a.at("desc").get<std::string>(), 221));
            // The client numbers the root "and" of `conds` 0 and its conditions from 1.
            int index = 0;
            for (auto& c : a.at("conds")) {
                Condition cond;
                cond.event = c.at("event").get<std::string>();
                cond.key = std::string(base) + "_" + std::to_string(++index);
                if (c.contains("tags")) {
                    for (auto& [key, value] : c.at("tags").items()) cond.tags.push_back(parseTag(key, value));
                }
                conditions.push_back(std::move(cond));
            }
        }
    }

    void event(Client* client, std::string_view event, std::initializer_list<Tag> tags) {
        if (!config::enableAchievements || !client || client->achievementsBlocked) return;
        // Sandbox (any arena that hands out cheats) never counts.
        if (client->game->arena->arenaData.flags & ArenaFlags::canUseCheats) return;

        for (const Condition& cond : conditions) {
            if (cond.event != event) continue;
            bool ok = true;
            for (const CondTag& want : cond.tags) {
                const Tag* got = nullptr;
                for (const Tag& t : tags) if (t.key == want.key) { got = &t; break; }
                if (!got || !matches(want, *got)) { ok = false; break; }
            }
            if (ok) client->pendingAchievements.push_back(cond.key);
        }
    }
}
