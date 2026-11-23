#pragma once

#include <functional>
#include <string>

#include "Game.h"

class Client;

namespace host {

// A private mode (sandbox) gives each connection a fresh instance instead of one shared game; a
// party code at Init moves it into another. A lobby mode (survival) puts each connection in its
// game still waiting to start.
enum class Instancing { Shared, Private, Lobbies };

struct Mode {
    std::string id, gamemode, name;
    ArenaFactory factory;
    Instancing instancing = Instancing::Shared;
    // The game every connection joins, for a shared mode.
    GameServer* shared = nullptr;
};

void init();
// Modes are listed in /api/servers in the order they are added.
Mode& addMode(const char* gamemode, const char* name, ArenaFactory factory,
              Instancing instancing = Instancing::Shared);
void addStandardModes();
// Every mspt, after the games tick.
void onTick(std::function<void()> fn);
// A socket closed, before its client is torn down.
void onClose(std::function<void(Client*)> fn);
int run();

}
