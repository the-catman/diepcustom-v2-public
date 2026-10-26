#include "App.h"

#include <cstdint>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "Client.h"
#include "Game.h"
#include "Config.h"
#include "Util.h"
#include "Const/Enums.h"
#include "Const/Achievements.h"
#include "Const/TankDefinitions.h"
#include "Const/Commands.h"

#include "Gamemodes/FFA.h"
#include "Gamemodes/Sandbox.h"
#include "Gamemodes/Team2.h"
#include "Gamemodes/Team4.h"
#include "Gamemodes/Domination.h"
#include "Gamemodes/Tag.h"
#include "Gamemodes/Mothership.h"
#include "Gamemodes/Maze.h"
#include "Gamemodes/Survival.h"

struct PerSocketData {
    Client* client = nullptr;
    std::string ipAddress;
    std::string gamemode;
};

static std::unordered_map<std::string, int> connections;

// The listed modes, in /api/servers order. A private mode (sandbox) gives each connection a
// fresh instance instead of one shared game; a party code at Init moves it into another.
struct Mode {
    std::string id, gamemode, name;
    ArenaFactory factory;
    GameServer* shared = nullptr;
};
static std::vector<Mode> modes;
static std::string tanksJson;

static std::string readFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return "";
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

static std::string mimeFor(const std::string& path) {
    auto ends = [&](const char* e) { size_t n = std::strlen(e); return path.size() >= n && path.compare(path.size() - n, n, e) == 0; };
    if (ends(".html")) return "text/html; charset=utf-8";
    if (ends(".js")) return "application/javascript; charset=utf-8";
    if (ends(".wasm")) return "application/wasm";
    if (ends(".css")) return "text/css; charset=utf-8";
    if (ends(".json")) return "application/json";
    if (ends(".png")) return "image/png";
    if (ends(".ico")) return "image/x-icon";
    return "application/octet-stream";
}

int main() {
    config::init();
    initTankDefinitions();
    achievements::init();
    tanksJson = readFile("data/TankDefinitions.json");

    const int PORT = config::serverPort;

    // Server ids count up from "aaaa" in list order.
    auto add = [](const char* gamemode, const char* name, ArenaFactory factory, bool isPrivate = false) {
        Mode m{std::string(4, 'a'), gamemode, name, std::move(factory)};
        for (size_t i = 3, n = modes.size(); n; --i, n /= 26) m.id[i] = (char)('a' + n % 26);
        if (!isPrivate) {
            m.shared = new GameServer(m.factory, m.gamemode, m.name);
            m.shared->id = m.id;
        }
        modes.push_back(std::move(m));
    };
    add("ffa", "FFA", [](GameServer* g) { return (ArenaEntity*)new FFAArena(g); });
    add("survival", "Survival", [](GameServer* g) { return (ArenaEntity*)new SurvivalArena(g); });
    add("teams", "2 Teams", [](GameServer* g) { return (ArenaEntity*)new Teams2Arena(g); });
    add("4teams", "4 Teams", [](GameServer* g) { return (ArenaEntity*)new Teams4Arena(g); });
    add("dom", "Domination", [](GameServer* g) { return (ArenaEntity*)new DominationArena(g); });
    add("tag", "Tag", [](GameServer* g) { return (ArenaEntity*)new TagArena(g); });
    add("maze", "Maze", [](GameServer* g) { return (ArenaEntity*)new MazeArena(g); });
    add("sandbox", "Sandbox", [](GameServer* g) { return (ArenaEntity*)new SandboxArena(g); }, true);
    add("mot", "Mothership", [](GameServer* g) { return (ArenaEntity*)new MothershipArena(g); });

    uWS::App app;

    app.ws<PerSocketData>("/*", {
        .compression = uWS::SHARED_COMPRESSOR,
        .maxPayloadLength = config::wssMaxMessageSize,
        .idleTimeout = 10,
        .upgrade = [](auto* res, auto* req, auto* context) {
            PerSocketData data;
            data.gamemode = std::string(req->getUrl().substr(1));
            res->template upgrade<PerSocketData>(std::move(data),
                req->getHeader("sec-websocket-key"), req->getHeader("sec-websocket-protocol"),
                req->getHeader("sec-websocket-extensions"), context);
        },
        .open = [](auto* ws) {
            PerSocketData* sd = ws->getUserData();
            std::string ip = std::string(ws->getRemoteAddressAsText());

            int conns = connections.count(ip) ? connections[ip] : 0;
            if (conns >= config::connectionsPerIp || bannedClients.count(ip)) { ws->close(); return; }

            Mode* mode = nullptr;
            for (Mode& m : modes) if (m.gamemode == sd->gamemode) { mode = &m; break; }
            if (!mode) { ws->close(); return; }
            GameServer* game = mode->shared;
            if (!game) {
                game = new GameServer(mode->factory, mode->gamemode, mode->name);
                game->id = mode->id;
                for (int i = 0; i < 8; ++i) game->partyCode += (char)(int)(util::randomFloat() * 256);
                GameServer::instances.push_back(game);
            }

            connections[ip] = conns + 1;
            Client* client = new Client(game);
            client->ipAddress = ip;
            client->sendRaw = [ws](const uint8_t* d, size_t n) {
                ws->send(std::string_view((const char*)d, n), uWS::OpCode::BINARY, true);
            };
            client->closeWs = [ws]() { ws->close(); };
            sd->client = client;
            sd->ipAddress = ip;
        },
        .message = [](auto* ws, std::string_view message, uWS::OpCode opCode) {
            PerSocketData* sd = ws->getUserData();
            if (!sd->client) return;
            sd->client->onMessage((const uint8_t*)message.data(), message.size(), opCode == uWS::OpCode::BINARY);
        },
        .close = [](auto* ws, int, std::string_view) {
            PerSocketData* sd = ws->getUserData();
            if (sd->client) {
                auto connection = connections.find(sd->ipAddress);
                if (connection != connections.end()) {
                    if (connection->second <= 1) connections.erase(connection);
                    else connection->second -= 1;
                }
                Client* c = sd->client;
                sd->client = nullptr;
                c->onClose();
                c->game->deadClients.push_back(c); // freed at end of the next tickLoop
            }
        }
    });

    app.get("/*", [](auto* res, auto* req) {
        res->onAborted([]() {});
        std::string url(req->getUrl());

        if (config::enableApi && url.rfind("/" + config::apiLocation, 0) == 0) {
            std::string sub = url.substr(config::apiLocation.size() + 1);
            // Clients served from another origin (static dist + ?server=) fetch these.
            auto ok = [&]() {
                res->writeStatus("200 OK");
                res->writeHeader("Access-Control-Allow-Origin", "*");
                return res;
            };
            if (sub == "/" || sub.empty()) { ok()->end(); return; }
            if (sub == "/tanks") { ok()->end(tanksJson); return; }
            if (sub == "/servers") {
                std::string j = "[";
                for (size_t i = 0; i < modes.size(); ++i) {
                    if (i) j += ",";
                    j += "{\"gamemode\":\"" + modes[i].gamemode + "\",\"name\":\"" + modes[i].name +
                         "\",\"id\":\"" + modes[i].id + "\"}";
                }
                j += "]";
                ok()->end(j);
                return;
            }
            if (sub == "/commands") { ok()->end(commandsJson()); return; }
            if (sub == "/colors") {
                std::string j = "{";
                for (int c = 0; c <= Color::kMaxColors; ++c) {
                    if (c) j += ",";
                    j += "\"" + std::to_string(c) + "\":" + std::to_string(colorHex(c));
                }
                j += "}";
                ok()->end(j);
                return;
            }
        }

        if (config::enableClient) {
            if (url.find("..") != std::string::npos) {
                res->writeStatus("403 Forbidden")->end();
                return;
            }
            std::string urlPath = url == "/" ? "/index.html" : url;
            std::string file = config::clientLocation + urlPath;
            std::string body = readFile(file);
            if (!body.empty()) {
                res->writeStatus("200 OK");
                res->writeHeader("Content-Type", mimeFor(urlPath));
                res->end(body);
                return;
            }
            res->writeStatus("404 Not Found");
            res->writeHeader("Content-Type", "text/html; charset=utf-8");
            res->end(readFile(config::clientLocation + "/404.html"));
            return;
        }
        res->writeStatus("404 Not Found")->end();
    });

    struct us_timer_t* timer = us_create_timer((struct us_loop_t*)uWS::Loop::get(), 0, 0);
    us_timer_set(timer, [](struct us_timer_t*) {
        for (Mode& m : modes) {
            if (!m.shared) continue;
            if (m.shared->clients.size()) m.shared->tickLoop();
            else m.shared->flushDeadClients(); // last client left; nobody ticks to free it otherwise
        }
        // A private instance goes as soon as it empties.
        auto& instances = GameServer::instances;
        for (size_t i = 0; i < instances.size();) {
            GameServer* g = instances[i];
            if (g->clients.size()) {
                g->tickLoop();
                ++i;
                continue;
            }
            g->flushDeadClients();
            instances.erase(instances.begin() + i);
            delete g;
        }
    }, (int)config::mspt, (int)config::mspt);

    app.listen(PORT, [PORT](auto* token) {
        if (token) util::log("Listening on port ", PORT);
        else util::log("Server failed to listen on port ", PORT);
    });

    app.run();
    return 0;
}
