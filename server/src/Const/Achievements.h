#pragma once

#include <cstdint>
#include <initializer_list>
#include <string>
#include <string_view>

class Client;

// Server side of the achievements. Every condition in data/Achievements.json is a counter
// the client keeps in localStorage ("A::<key>"); the server sends a condition's key once per
// event that matches it and the client counts it toward the threshold.
namespace achievements {
    struct Tag {
        enum Kind : uint8_t { Bool, Number, String };

        std::string_view key;
        Kind kind;
        double number = 0;
        std::string_view string;

        Tag(std::string_view key, bool value) : key(key), kind(Bool), number(value) {}
        Tag(std::string_view key, int value) : key(key), kind(Number), number(value) {}
        Tag(std::string_view key, double value) : key(key), kind(Number), number(value) {}
        Tag(std::string_view key, const std::string& value) : key(key), kind(String), string(value) {}
    };

    void init();
    // Queues the key of every condition of `event` whose tags all match, unless the client has
    // cheated this connection or the arena allows cheats.
    void event(Client* client, std::string_view event, std::initializer_list<Tag> tags);
}
