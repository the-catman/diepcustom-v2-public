#include "Game.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>
#include <emscripten/emscripten.h>
#include <emscripten/html5.h>

#include "Achievements.h"
#include "Const.h"
#include "coder/Reader.h"
#include "TankDefs.h"

static constexpr double PING_THROTTLE_MS = 500;

EM_JS(void, load_password, (char* out, int max), {
    stringToUTF8(window.localStorage.getItem("password") || "", out, max);
});

// The last gamemode connected to, saved for the next visit.
EM_JS(void, gamemode_load, (char* out, int max), {
    stringToUTF8(window.localStorage.getItem("gamemode") || "", out, max);
});

EM_JS(void, gamemode_save, (const char* mode), {
    window.localStorage.setItem("gamemode", UTF8ToString(mode));
});

EM_JS(void, party_hash_read, (char* out, int max), {
    stringToUTF8(window.location.hash, out, max);
});

EM_JS(void, party_invalid_alert, (), { alert("Invalid party ID"); });
EM_JS(void, party_hash_wipe, (), { window.location.hash = ""; });

EM_JS(void, party_link_prefix, (char* out, int max), {
    stringToUTF8(location.host + location.pathname + location.search + "#", out, max);
});

// Party bytes travel as two hex digits each, low nibble first. Reading them back is
// case-insensitive, and a trailing odd digit becomes a low-nibble-only byte.
static std::string partyHex(const std::string& bytes) {
    static const char* digits = "0123456789ABCDEF";
    std::string out;
    for (unsigned char b : bytes) {
        out += digits[b & 15];
        out += digits[b >> 4];
    }
    return out;
}

static std::string partyUnhex(const std::string& hex) {
    auto nibble = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        return 0;
    };
    std::string out;
    for (size_t i = 0; i < hex.size(); i += 2) {
        int b = nibble(hex[i]);
        if (i + 1 < hex.size()) b |= nibble(hex[i + 1]) << 4;
        out += (char)b;
    }
    return out;
}

// F2 fullscreen toggle.
EM_JS(void, toggle_fullscreen, (), {
    if (!document.fullscreenElement && !document.webkitFullscreenElement && !document.mozFullscreenElement) {
        var e = document.body;
        if (!e) return;
        if (e.requestFullscreen) e.requestFullscreen();
        else if (e.webkitRequestFullscreen) e.webkitRequestFullscreen();
        else if (e.mozRequestFullScreen) e.mozRequestFullScreen();
    } else if (document.exitFullscreen) {
        document.exitFullscreen();
    } else if (document.mozCancelFullScreen) {
        document.mozCancelFullScreen();
    } else if (document.webkitExitFullscreen) {
        document.webkitExitFullscreen();
    }
});

// Gamepad 0 if it uses the standard mapping, read in one call.
EM_JS(int, pad_poll, (float* axes, uint8_t* buttons), {
    var pads = navigator.getGamepads ? navigator.getGamepads() : null;
    var pad = pads && pads[0];
    if (!pad || pad["mapping"] != "standard") return 0;
    for (var i = 0; i < 4; ++i) HEAPF32[(axes >> 2) + i] = i < pad.axes.length ? pad.axes[i] : 0;
    for (var i = 0; i < 16; ++i) HEAPU8[buttons + i] = i < pad.buttons.length && pad.buttons[i].pressed ? 1 : 0;
    return 1;
});

Game& Game::get() {
    static Game instance;
    return instance;
}

struct InputCallbacks {
    // Keys the game reads; these skip the browser's default action outside text fields.
    static bool gameKey(int key) {
        switch (key) {
            case Key::Enter: case Key::Shift: case Key::Space: case Key::Home:
            case Key::Left: case Key::Up: case Key::Right: case Key::Down:
            case Key::A: case Key::C: case Key::D: case Key::E: case Key::H: case Key::I: case Key::K:
            case Key::L: case Key::M: case Key::O: case Key::S: case Key::U: case Key::W: case Key::Y:
            case Key::F2: case Key::SemicolonFirefox: case Key::Semicolon: case Key::Backslash:
                return true;
        }
        return (key >= Key::Digit1 && key < Key::Digit1 + 8) || (key >= Key::Numpad1 && key < Key::Numpad1 + 8);
    }
    // keyCode, so letters follow the layout. Repeats are dropped and F1/F3-F19 are left to the
    // browser. Modifiers don't stop a key from registering.
    static EM_BOOL key(int type, const EmscriptenKeyboardEvent* e, void*) {
        Game& game = Game::get();
        bool down = type == EMSCRIPTEN_EVENT_KEYDOWN;
        int key = (int)e->keyCode;
        // Tab never reaches the browser (it would move focus off the text field).
        if (key == Key::Tab) return EM_TRUE;
        if (key >= 112 && key <= 130 && key != Key::F2) return EM_FALSE;
        bool modifier = e->ctrlKey || e->metaKey || e->altKey;
        EM_BOOL result = !modifier && !game.ui.typing() && gameKey(key) ? EM_TRUE : EM_FALSE;
        if (down && e->repeat) return result;
        game.setKey(key, down);
        if (!down) return result;
        if (key == Key::F2) {
            if (!game.ui.capturesKeys()) toggle_fullscreen();
            return EM_TRUE;
        }
        if (game.ui.onKeyDown(key, game)) return EM_TRUE;
        if (game.ui.capturesKeys()) return result;
        // Dev cheats: ';' invincibility, '\' switchtank, H take tank.
        switch (key) {
            case Key::Semicolon: case Key::SemicolonFirefox: game.invincibilityLatch = true; break;
            case Key::Backslash: game.switchTankLatch = true; break;
            case Key::H: game.sendTakeTank(); break;
            case Key::E: game.toggleAutoFire(); break;
            case Key::C: game.toggleAutoSpin(); break;
            case Key::L: game.showPing = !game.showPing; break;
            case Key::I: if (game.padConnected) game.toggleGamepad(); break;
        }
        return result;
    }
    static EM_BOOL mouse(int type, const EmscriptenMouseEvent* e, void*) {
        Game& game = Game::get();
        game.mouseScreenX = e->targetX;
        game.mouseScreenY = e->targetY;
        // The mouse export reminds gamepad users that I turns it off, at most every 5s.
        if (type == EMSCRIPTEN_EVENT_MOUSEMOVE && game.padActive()) {
            double now = emscripten_get_now();
            if (game.padNoticeAt < 0 || now - game.padNoticeAt > 5000) {
                game.padNoticeAt = now;
                game.ui.addNotification("Using Gamepad, press I to disable", 0x0000ff, 5000, "using_gamepad", now);
            }
        }
        if (type == EMSCRIPTEN_EVENT_MOUSEDOWN || type == EMSCRIPTEN_EVENT_MOUSEUP) {
            bool down = type == EMSCRIPTEN_EVENT_MOUSEDOWN;
            game.setKey(e->button + 1, down);
            // UI presses read mouse 1 through the same console-gated key map.
            if (e->button == 0 && !game.ui.capturesKeys()) {
                if (down) game.ui.onMouseDown(game);
                else game.ui.onMouseUp(game);
            }
        }
        return EM_TRUE;
    }
    static EM_BOOL wheel(int type, const EmscriptenWheelEvent* e, void*) {
        (void)type;
        // 100px or 3 lines per notch.
        double notches = e->deltaY;
        if (e->deltaMode == DOM_DELTA_PIXEL) notches /= 100.0;
        else if (e->deltaMode == DOM_DELTA_LINE) notches /= 3.0;
        return Game::get().ui.onWheel(notches) ? EM_TRUE : EM_FALSE;
    }
};

void Game::boot(const std::string& mode) {
    char saved[64] = {0};
    gamemode_load(saved, sizeof(saved));
    gamemode = saved[0] ? saved : mode;
    // ?canvas2d draws the world with Canvas2D (the fallback when WebGL2 is missing).
    if (EM_ASM_INT({ return new URLSearchParams(location.search).has("canvas2d") ? 1 : 0; })) config::renWorldGL = false;
    cp5::backendInit("#canvas");
    cp5::backendBeginFrame();
    screen = std::make_unique<cp5::Context>(false);
    installConsoleApi();

    emscripten_set_keydown_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, nullptr, EM_TRUE, InputCallbacks::key);
    emscripten_set_keyup_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, nullptr, EM_TRUE, InputCallbacks::key);
    emscripten_set_mousemove_callback("#canvas", nullptr, EM_TRUE, InputCallbacks::mouse);
    emscripten_set_mousedown_callback("#canvas", nullptr, EM_TRUE, InputCallbacks::mouse);
    emscripten_set_mouseup_callback("#canvas", nullptr, EM_TRUE, InputCallbacks::mouse);
    emscripten_set_wheel_callback("#canvas", nullptr, EM_TRUE, InputCallbacks::wheel);

    connection.onOpen = [this]() { onOpen(); };
    connection.onData = [this](const uint8_t* d, size_t n) { onData(d, n); };
    connection.onClose = [this]() { onClose(); };

    char host[256] = {0};
    EM_ASM({
        const server = new URLSearchParams(location.search).get("server");
        stringToUTF8(server || location.host, $0, 255);
    }, host);
    wsHost = host;
    openSocket();
}

void Game::openSocket() {
    readPartyHash();
    tankdefs::fetch();
    joiningParty = !partyServer.empty();
    if (!joiningParty) connectSocket();
}

void Game::connectSocket() {
    gamemode_save(gamemode.c_str());
    connection.connect(socketUrl());
}

// "#<hex>" holds the server id, a 0 byte, then the party code.
void Game::readPartyHash() {
    char buf[512] = {0};
    party_hash_read(buf, sizeof(buf));
    std::string hash = buf;
    party.clear();
    partyServer.clear();
    if (hash.size() < 2) return;
    std::string bytes = partyUnhex(hash.substr(1));
    size_t zero = bytes.find('\0');
    partyServer = bytes.substr(0, zero);
    if (zero != std::string::npos) party = bytes.substr(zero + 1);
}

std::string Game::partyLink() const {
    char prefix[512] = {0};
    party_link_prefix(prefix, sizeof(prefix));
    std::string id = tankdefs::serverId(gamemode);
    return prefix + partyHex((id.empty() ? gamemode : id) + '\0' + party);
}

std::string Game::socketUrl() const {
    bool secure = config::netForceSecure || EM_ASM_INT({ return location.protocol === "https:" ? 1 : 0; });
    return (secure ? "wss://" : "ws://") + wsHost + '/' + gamemode;
}

void Game::reconnect() {
    connection.close();
    simulation = Simulation{};
    accepted = false;
    spawnedThisConnection = false;
    reconnectCountdown = 0;
    pongReceived = true;
    pingSamples.clear();
    autoFire = false;
    autoSpin = false;
    playersOnline = -1;
    openSocket();
}

void Game::switchGamemode(const std::string& mode) {
    if (mode.empty() || mode == gamemode) return;
    // A party link names its own gamemode; picking another one leaves the party.
    party_hash_wipe();
    gamemode = mode;
    reconnect();
}

void Game::onOpen() {
    spawnedThisConnection = false;
    deathPending = false;
    partyReceived = false;
    ui.onNewConnection();
    rendering.onNewConnection();
    char pw[128] = {0};
    load_password(pw, sizeof(pw));
    Writer w;
    w.u8(ServerBound::Init).stringNT(config::BUILD_HASH).stringNT(pw).stringNT(partyHex(party)).u8(0);
    connection.send(w);
}

void Game::onClose() {
    std::printf("connection closed\n");
    accepted = false;
    spawnedThisConnection = false;
    simulation = Simulation{};
    autoFire = false;
    autoSpin = false;
    playersOnline = -1;
    pongReceived = true;
    pingSamples.clear();
    // No page reload once the backoff gets long, so it just keeps growing.
    reconnectCountdown = reconnectBackoff;
    reconnectBackoff = reconnectBackoff * 2 + 20;
}

void Game::onData(const uint8_t* data, size_t len) {
    if (len == 0) return;
    Reader r(data, len);
    uint8_t header = r.u8();

    switch (header) {
        case ClientBound::Update:
            reconnectBackoff = 1;
            simulation.decodeUpdate(r, emscripten_get_now());
            return;
        case ClientBound::Ping:
            pongReceived = true;
            pingSamples.push_back(emscripten_get_now() - lastPingSent);
            while ((int)pingSamples.size() > std::max(0, config::netLatencyHistorySize))
                pingSamples.erase(pingSamples.begin());
            achievements::onLatency(pingAverage());
            return;
        case ClientBound::ServerInfo: {
            std::string mode = r.stringNT();
            serverHost = r.stringNT();
            std::printf("server info: %s @ %s\n", mode.c_str(), serverHost.c_str());
            return;
        }
        case ClientBound::PlayerCount:
            playersOnline = (int)r.vu();
            return;
        case ClientBound::Accept: {
            accepted = true;
            int level = r.vi();
            commandLevel = std::max(commandLevel, level);
            std::printf("accepted, access level %d\n", level);
            return;
        }
        case ClientBound::Notification: {
            std::string text = r.stringNT();
            uint32_t color = r.u32();
            float time = r.floatLE();
            std::string id = r.stringNT();
            ui.addNotification(text, color, time, id, emscripten_get_now());
            return;
        }
        case ClientBound::Achievement: {
            std::vector<std::string> keys;
            for (uint32_t n = r.vu(); n > 0; --n) keys.push_back(r.stringNT());
            achievements::onKeys(keys);
            return;
        }
        case ClientBound::PartyCode:
            party.assign((const char*)data + r.at, len - r.at);
            partyReceived = true;
            return;
        case ClientBound::InvalidParty:
            party_invalid_alert();
            party_hash_wipe();
            reconnect();
            return;
        case ClientBound::OutdatedClient:
            // Log the build and reload the page.
            std::printf("%s\n", r.stringNT().c_str());
            EM_ASM({ setTimeout(() => window.location.reload()); });
            return;
        case ClientBound::Compressed:
            std::printf("unexpected compressed packet\n");
            return;
        default:
            return;
    }
}

void Game::sendSpawn(const std::string& name) {
    if (!accepted) return;
    Writer w;
    w.u8(ServerBound::Spawn).stringNT(name);
    connection.send(w);
}

void Game::sendRespawn() {
    if (!accepted) return;
    Writer w;
    w.u8(ServerBound::ToRespawn);
    connection.send(w);
}

// targetLevel: queue drains send the level they aim for, direct presses send -1.
void Game::sendStatUpgrade(int statId, int targetLevel) {
    if (!accepted) return;
    Writer w;
    w.u8(ServerBound::StatUpgrade).vi(statId ^ (int)(config::MAGIC_NUM % 8)).vi(targetLevel);
    connection.send(w);
}

void Game::sendTankUpgrade(int tankId) {
    if (!accepted) return;
    int tankCount = tankdefs::count();
    if (tankCount <= 0) return;
    Writer w;
    w.u8(ServerBound::TankUpgrade).vi(tankId ^ (int)(config::MAGIC_NUM % (uint32_t)tankCount));
    connection.send(w);
}

void Game::sendTakeTank() {
    if (!accepted) return;
    Writer w;
    w.u8(ServerBound::TakeTank);
    connection.send(w);
}

void Game::sendCommand(const std::string& cmd, const std::vector<std::string>& args) {
    if (!accepted) return;
    size_t n = std::min<size_t>(args.size(), 255);
    Writer w;
    w.u8(ServerBound::TCPInit).stringNT(cmd).u8((int)n);
    for (size_t i = 0; i < n; ++i) w.stringNT(args[i]);
    connection.send(w);
}

double Game::pingAverage() const {
    if (pingSamples.empty()) return -1;
    double sum = 0;
    for (double s : pingSamples) sum += s;
    return sum / (double)pingSamples.size();
}

void Game::toggleAutoFire() {
    Entity* camera = simulation.getCamera();
    if (!camera || !camera->camera || !simulation.get(camera->camera->player)) return;
    autoFire = !autoFire;
    ui.addNotification(autoFire ? "Auto Fire: ON" : "Auto Fire: OFF", 0x0000ff, 5000, "auto_fire", emscripten_get_now());
}

void Game::toggleAutoSpin() {
    Entity* camera = simulation.getCamera();
    if (!camera || !camera->camera) return;
    Entity* player = simulation.get(camera->camera->player);
    if (!player) return;
    autoSpin = !autoSpin;
    if (autoSpin && player->position) {
        double now = emscripten_get_now();
        spinPhase = player->position->angle.getAngle(now) - now / 1000.0;
    }
    ui.addNotification(autoSpin ? "Auto Spin: ON" : "Auto Spin: OFF", 0x0000ff, 5000, "auto_spin", emscripten_get_now());
}

// Effective mouse in world coords; orbits the player at r=700, 1 rad/s while autospinning.
bool Game::mouseWorld(double now, double& mx, double& my) {
    if (autoSpin) {
        Entity* camera = simulation.getCamera();
        Entity* player = camera && camera->camera ? simulation.get(camera->camera->player) : nullptr;
        if (player && player->position) {
            double a = now / 1000.0 + spinPhase;
            mx = player->position->x.getLinear(now) + std::cos(a) * 700.0;
            my = player->position->y.getLinear(now) + std::sin(a) * 700.0;
            return true;
        }
    }
    double scale = rendering.zoom();
    if (scale <= 0) return false;
    // The right stick aims 1200u (screen) from the player and keeps the last direction inside
    // the 0.1 dead zone.
    if (padActive()) {
        Entity* camera = simulation.getCamera();
        Entity* player = camera && camera->camera ? simulation.get(camera->camera->player) : nullptr;
        if (player && player->position) {
            if (std::sqrt(padAxes[2] * padAxes[2] + padAxes[3] * padAxes[3]) >= 0.1) {
                padAimX = (float)(padAxes[2] * 1.1);
                padAimY = (float)(padAxes[3] * 1.1);
                float len = std::sqrt(padAimX * padAimX + padAimY * padAimY);
                if (len > 1) { padAimX /= len; padAimY /= len; }
            }
            double ui = std::max(cp5::canvasWidth() / 1920.0, cp5::canvasHeight() / 1080.0) * config::uiScale;
            mx = player->position->x.getLinear(now) + padAimX * 1200.0 * ui / scale;
            my = player->position->y.getLinear(now) + padAimY * 1200.0 * ui / scale;
            return true;
        }
    }
    double dpr = cp5::pixelRatio();
    mx = rendering.cameraX() + (mouseScreenX * dpr - cp5::canvasWidth() / 2.0) / scale;
    my = rendering.cameraY() + (mouseScreenY * dpr - cp5::canvasHeight() / 2.0) / scale;
    return true;
}

void Game::toggleGamepad() {
    gamepadEnabled = !gamepadEnabled;
    ui.addNotification(gamepadEnabled ? "Gamepad enabled" : "Gamepad disabled", 0x0000ff, 5000, "gamepad_enabled",
                       emscripten_get_now());
}

// Axes ease halfway to the raw value each frame (0 while the console is open); button
// edges are per frame. Nothing updates while no standard gamepad is connected.
void Game::pollGamepad() {
    float axes[4];
    uint8_t buttons[16];
    padConnected = pad_poll(axes, buttons) != 0;
    if (!padConnected) return;
    for (int i = 0; i < 4; ++i) padAxes[i] = (padAxes[i] + (ui.capturesKeys() ? 0.0f : axes[i])) * 0.5f;
    for (int i = 0; i < 16; ++i) {
        bool held = buttons[i] != 0;
        padPressed[i] = held && !padHeld[i];
        padHeld[i] = held;
    }
}

// Left stick: 0.1 dead zone, then scaled by 1.1 and clamped to length 1.
bool Game::padMove(float& x, float& y) const {
    x = y = 0;
    if (std::sqrt(padAxes[0] * padAxes[0] + padAxes[1] * padAxes[1]) < 0.1) return false;
    x = (float)(padAxes[0] * 1.1);
    y = (float)(padAxes[1] * 1.1);
    float len = std::sqrt(x * x + y * y);
    if (len > 1) { x /= len; y /= len; }
    return true;
}

bool Game::keyHeld(int key) const {
    return key >= 0 && key < 256 && keysHeld[key] && !ui.capturesKeys();
}

void Game::setKey(int key, bool down) {
    if (key >= 0 && key < 256) keysHeld[key] = down;
}

// Mouse 1 or Space, unless the cursor is over a UI element.
bool Game::firing() const {
    if (ui.blocksFire()) return false;
    if (padActive()) return padHeld[7]; // RT
    return keyHeld(Key::Mouse1) || keyHeld(Key::Space);
}

// Input flags from the held keys (fire and the latches are added by sendInputs).
uint32_t Game::heldFlags() const {
    uint32_t flags = 0;
    if (padActive()) {
        // The left stick moves (sent as two floats after the mouse); WASD is ignored.
        flags |= InputFlags::gamepad;
    } else {
        if (keyHeld(Key::W) || keyHeld(Key::Up)) flags |= InputFlags::up;
        if (keyHeld(Key::A) || keyHeld(Key::Left)) flags |= InputFlags::left;
        if (keyHeld(Key::S) || keyHeld(Key::Down)) flags |= InputFlags::down;
        if (keyHeld(Key::D) || keyHeld(Key::Right)) flags |= InputFlags::right;
    }
    if (keyHeld(Key::O)) flags |= InputFlags::suicide;
    bool right = padActive() ? padHeld[6] : keyHeld(Key::Mouse3) || keyHeld(Key::Shift); // LT
    if (!ui.blocksFire() && right) flags |= InputFlags::rightclick;
    if (keyHeld(Key::K)) flags |= InputFlags::levelup;
    return flags;
}

void Game::sendInputs(double now) {
    Entity* camera = simulation.getCamera();
    if (!camera || !camera->camera) return;
    if (now - lastInputSent <= 30) return;
    lastInputSent = now;

    double mouseX, mouseY;
    if (!mouseWorld(now, mouseX, mouseY)) return;

    uint32_t flags = heldFlags();
    if (firing() || fireLatch || autoFire) flags |= InputFlags::leftclick;
    fireLatch = false;
    if (invincibilityLatch) { flags |= InputFlags::invincibility; invincibilityLatch = false; }
    if (switchTankLatch) { flags |= InputFlags::switchtank; switchTankLatch = false; }

    Writer w;
    w.u8(ServerBound::Input).vu(flags).vf(mouseX).vf(mouseY);
    if (flags & InputFlags::gamepad) {
        float x, y;
        padMove(x, y);
        w.vf(x).vf(y);
    }
    connection.send(w);
}

void Game::predictTick(double now) {
    Entity* camera = simulation.getCamera();
    Entity* player = camera && camera->camera ? simulation.get(camera->camera->player) : nullptr;
    // Everything but the own tank: a position the client took over goes back to the network.
    for (auto& [id, e] : simulation.entities) {
        if (&e == player || !e.position) continue;
        e.position->x.disown();
        e.position->y.disown();
    }
    if (!player || !player->position) return;
    PositionData& pos = *player->position;
    double dt = rendering.dt();

    double mx, my;
    if (mouseWorld(now, mx, my)) {
        pos.angle.snap((float)std::atan2(my - pos.y.getLinear(now), mx - pos.x.getLinear(now)));
    }

    float padX, padY;
    if (padActive()) {
        if (padMove(padX, padY)) {
            float o = (float)(camera->camera->movementSpeed * dt / 40.0);
            pos.offX += padX * o;
            pos.offY += padY * o;
        }
    } else if (camera->camera->movementSpeed > 0) {
        uint32_t flags = heldFlags();
        bool up = flags & InputFlags::up, left = flags & InputFlags::left;
        bool down = flags & InputFlags::down, right = flags & InputFlags::right;
        float o = (float)(camera->camera->movementSpeed * dt / 40.0);
        if (!((up + left + down + right) & 1)) o *= 0.707107f;
        if (up) pos.offY -= o;
        if (left) pos.offX -= o;
        if (down) pos.offY += o;
        if (right) pos.offX += o;
    }

    // The offsets drift into the position, which the client then owns.
    float n = (float)(dt / 40.0);
    float decay = std::pow(0.9f, n);
    float merge = (1.0f - decay) / 0.1f;
    if (std::fabs(pos.offX) < 0.01) {
        pos.offX = 0;
    } else {
        pos.x.own();
        pos.x.target += pos.offX * merge;
        pos.offX *= decay;
    }
    if (std::fabs(pos.offY) < 0.01) {
        pos.offY = 0;
    } else {
        pos.y.own();
        pos.y.target += pos.offY * merge;
        pos.offY *= decay;
    }
}

namespace {
    struct Pose { double x, y, angle; };
    // The frame think adds the own tank's offsets times clamp(dt / 40, 0, 2).
    struct OwnOffset { const Entity* tank; float scale; };

    // Arena clamp: the own tank, as a root, never sits more than 200 past the arena's edges;
    // its owned target takes up the difference. The position it checks includes the offsets
    // times offScale.
    void clampToArena(Simulation& sim, Entity& tank, double now, float offScale) {
        Entity* arena = sim.getArena();
        if (!arena || !arena->arena || !tank.position) return;
        if (tank.relations && sim.get(tank.relations->parent)) return;
        const ArenaData& a = *arena->arena;
        PositionData& p = *tank.position;
        float x = p.x.getLinear(now) + p.offX * offScale, y = p.y.getLinear(now) + p.offY * offScale;
        auto pull = [&](LerpVal& v, float edge, float at) {
            float shown = v.getLinear(now);
            v.own();
            v.target = edge + shown - at;
        };
        if (x < (float)a.leftX - 200.0f) pull(p.x, (float)a.leftX - 200.0f, x);
        if (y < (float)a.topY - 200.0f) pull(p.y, (float)a.topY - 200.0f, y);
        if (x > (float)a.rightX + 200.0f) pull(p.x, (float)a.rightX + 200.0f, x);
        if (y > (float)a.bottomY + 200.0f) pull(p.y, (float)a.bottomY + 200.0f, y);
    }

    // Where the entity is: its parents' transforms plus its own lerp.
    Pose worldPose(Simulation& sim, Entity& e, double now, const OwnOffset& own) {
        const PositionData& p = *e.position;
        double lx = p.x.getLinear(now), ly = p.y.getLinear(now), la = p.angle.getAngle(now);
        if (&e == own.tank) {
            lx += p.offX * own.scale;
            ly += p.offY * own.scale;
        }
        Entity* parent = e.relations ? sim.get(e.relations->parent) : nullptr;
        if (!parent || !parent->position) return {lx, ly, la};
        Pose o = worldPose(sim, *parent, now, own);
        double c = std::cos(o.angle), s = std::sin(o.angle);
        return {o.x + c * lx - s * ly, o.y + s * lx + c * ly,
                (p.flags & PositionFlags::absoluteRotation) ? la : o.angle + la};
    }

    Entity& rootOf(Simulation& sim, Entity& e) {
        Entity* r = &e;
        while (r->relations) {
            Entity* parent = sim.get(r->relations->parent);
            if (!parent) break;
            r = parent;
        }
        return *r;
    }

    bool alive(const Entity& e, double now) { return !e.health || e.health->health.getSmooth(now) > 0; }

    // sides 2 (and 0) collide as rectangles: size along the angle, width across it.
    bool isRect(const PhysicsData& p) { return p.sides < 3 && p.sides != 1; }

    // Shape overlap. Rectangles only meet circles, and a root never collides with itself.
    bool overlaps(Simulation& sim, Entity& a, Entity& b, double now, const OwnOffset& own) {
        if (&rootOf(sim, a) == &rootOf(sim, b)) return false;
        const PhysicsData& pa = *a.physics;
        const PhysicsData& pb = *b.physics;
        if ((pa.flags | pb.flags) & PhysicsFlags::doChildrenCollision) return false;
        if (!isRect(pa) && pb.sides == 2) return overlaps(sim, b, a, now, own);
        if (!a.position || !b.position) return false;
        Pose A = worldPose(sim, a, now, own), B = worldPose(sim, b, now, own);
        if (!isRect(pa) && !isRect(pb)) {
            double r = pa.size.getSmooth(now) + pb.size.getSmooth(now);
            return (A.x - B.x) * (A.x - B.x) + (A.y - B.y) * (A.y - B.y) < r * r;
        }
        if (pa.sides != 2 || isRect(pb)) return false;
        double c = std::cos(A.angle), s = std::sin(A.angle);
        double dx = B.x - A.x, dy = B.y - A.y;
        double lx = A.x + dx * c + dy * s, ly = A.y - dx * s + dy * c;
        double size = pa.size.getSmooth(now), width = pa.width.getSmooth(now);
        double minX = A.x - size * 0.5, minY = A.y - width * 0.5;
        double cx = std::min(std::max(lx, minX), minX + size), cy = std::min(std::max(ly, minY), minY + width);
        double r = pb.size.getSmooth(now);
        return (lx - cx) * (lx - cx) + (ly - cy) * (ly - cy) < r * r;
    }

    // a's collision flags against b's team (or b itself) and owner.
    bool collides(Simulation& sim, Entity& a, Entity& b) {
        auto team = [&](Entity& e) { Entity* t = e.relations ? sim.get(e.relations->team) : nullptr; return t ? t : &e; };
        auto owner = [&](Entity& e) { return e.relations ? sim.get(e.relations->owner) : nullptr; };
        uint32_t flags = a.physics->flags;
        if ((flags & PhysicsFlags::noOwnTeamCollision) && team(a) == team(b)) return false;
        if ((flags & PhysicsFlags::onlySameOwnerCollision) && team(a) == team(b) && owner(a) != owner(b)) return false;
        if ((flags & PhysicsFlags::isBase) && team(b) == sim.getArena()) return false;
        return true;
    }

    // The pusher kicks the pushed entity's root offset by push * absorb along the angle. A solid
    // wall first damps that offset to 0.3 and kicks 1/0.3 as hard, and a rectangular pusher
    // kicks along whichever of its axes the angle leans on.
    void kick(Simulation& sim, Entity& pusher, Entity& pushed, double angle, double now, const OwnOffset& own) {
        Entity& pushedRoot = rootOf(sim, pushed);
        if (!pushedRoot.position) return;
        PositionData* root = &*pushedRoot.position;
        const PhysicsData& p = *pusher.physics;
        double j = p.pushFactor * pushed.physics->absorbtionFactor;
        if (p.flags & PhysicsFlags::isSolidWall) {
            if (root->flags & PositionFlags::canMoveThroughWalls) return;
            root->offX *= 0.3f;
            root->offY *= 0.3f;
            j /= 0.3;
        }
        if (p.sides == 2) {
            double k = pusher.position ? worldPose(sim, pusher, now, own).angle : 0;
            double rel = angle - k;
            double along = std::cos(rel) / p.size.getSmooth(now), across = std::sin(rel) / p.width.getSmooth(now);
            double c = std::cos(k), s = std::sin(k);
            if (std::fabs(along) > std::fabs(across)) {
                double sign = along < 0 ? -1 : 1;
                root->offX += (float)(sign * c * j);
                root->offY += (float)(sign * s * j);
            } else {
                double sign = across < 0 ? -1 : 1;
                root->offX += (float)(-sign * s * j);
                root->offY += (float)(sign * c * j);
            }
            return;
        }
        root->offX += (float)(std::cos(angle) * j);
        root->offY += (float)(std::sin(angle) * j);
    }
}

// The own player's shape against everything it overlaps. Only pairs with a solid wall kick,
// each side away from the other.
void Game::wallKicks(double now) {
    Entity* camera = simulation.getCamera();
    Entity* player = camera && camera->camera ? simulation.get(camera->camera->player) : nullptr;
    if (!player || !player->physics || !player->position) return;
    if (player->physics->flags & PhysicsFlags::doChildrenCollision) return;
    if (!alive(*player, now)) return;
    OwnOffset own{player, (float)std::min(2.0, std::max(0.0, rendering.dt() / 40.0))};
    for (auto& [id, e] : simulation.entities) {
        if (&e == player || !e.physics || !e.position) continue;
        if (!((e.physics->flags | player->physics->flags) & PhysicsFlags::isSolidWall)) continue;
        if (!overlaps(simulation, *player, e, now, own) || !alive(e, now)) continue;
        if (!collides(simulation, *player, e) || !collides(simulation, e, *player)) continue;
        Entity& rootP = rootOf(simulation, *player);
        Entity& rootE = rootOf(simulation, e);
        if (!alive(rootP, now) || !alive(rootE, now) || !rootP.position || !rootE.position) continue;
        Pose a = worldPose(simulation, rootP, now, own), b = worldPose(simulation, rootE, now, own);
        double angle = std::atan2(b.y - a.y, b.x - a.x);
        kick(simulation, *player, e, angle, now, own);
        kick(simulation, e, *player, angle + M_PI, now, own);
    }
}

// Before the world draws: the own tank shows the network lerp, or with
// net_predict_movement eases from where prediction carried it toward the lerp by clamp(dt/300).
// The arena clamp runs on each side of it: in the frame think with the offsets at dt/40, and
// while drawing with them at 0.
void Game::settleOwnPosition(double now) {
    Entity* camera = simulation.getCamera();
    Entity* player = camera && camera->camera ? simulation.get(camera->camera->player) : nullptr;
    if (!player || !player->position) return;
    clampToArena(simulation, *player, now, (float)std::min(2.0, std::max(0.0, rendering.dt() / 40.0)));
    double k = std::min(1.0, std::max(0.0, rendering.dt() / 300.0));
    for (LerpVal* v : {&player->position->x, &player->position->y}) {
        float net = v->netLinear(now);
        float shown = config::netPredictMovement ? (float)((1.0 - k) * v->getLinear(now) + k * net) : net;
        v->own();
        v->target = shown;
    }
    clampToArena(simulation, *player, now, 0);
}

void Game::frame(double now) {
    pollGamepad();
    // L3 / R3 toggle auto fire / auto spin, like E and C.
    if (padPressed[10]) toggleAutoFire();
    if (padPressed[11]) toggleAutoSpin();
    Entity* cameraEnt = simulation.getCamera();
    if (!cameraEnt || !cameraEnt->camera || !simulation.get(cameraEnt->camera->player)) {
        autoFire = false;
        autoSpin = false;
    } else {
        spawnedThisConnection = true;
        deathPending = true;
    }
    if (cameraEnt && cameraEnt->camera && !(cameraEnt->camera->flags & CameraFlags::showingDeathStats))
        deathPending = false;
    if (reconnectCountdown > 0 && --reconnectCountdown == 0) {
        connection.close();
        openSocket();
    }
    // A saved gamemode the server no longer offers falls back to the first one.
    // An unknown server id alerts "Invalid party ID", wipes the hash and joins the current mode
    // without a party.
    if (joiningParty && tankdefs::serversSettled()) {
        joiningParty = false;
        std::string mode = tankdefs::serverGamemodeById(partyServer);
        if (mode.empty()) {
            party_invalid_alert();
            party_hash_wipe();
            party.clear();
        } else {
            gamemode = mode;
        }
        connectSocket();
    }
    if (!gamemodeChecked && !joiningParty && tankdefs::serverCount() > 0) {
        gamemodeChecked = true;
        bool offered = false;
        for (int i = 0; i < tankdefs::serverCount(); ++i) offered |= tankdefs::serverGamemode(i) == gamemode;
        if (!offered) switchGamemode(tankdefs::serverGamemode(0));
    }
    // /api/colors: one net_replace_color per entry.
    int colors[2 * 64];
    for (int i = 0, n = tankdefs::takeColors(colors, 64); i < n; ++i)
        ui.execute(*this, "net_replace_color " + std::to_string(colors[2 * i]) + ' ' + std::to_string(colors[2 * i + 1]));
    if (firing()) fireLatch = true;
    if (connection.isConnected() && accepted) {
        if (pongReceived && now - lastPingSent >= PING_THROTTLE_MS) {
            Writer w;
            w.u8(ServerBound::Ping);
            connection.send(w);
            lastPingSent = now;
            pongReceived = false;
        }
        sendInputs(now);
    }
    predictTick(now);
    wallKicks(now);
    settleOwnPosition(now);
    cp5::backendBeginFrame();
    if (screen->width() != cp5::canvasWidth() || screen->height() != cp5::canvasHeight())
        screen->setSize(cp5::canvasWidth(), cp5::canvasHeight());
    rendering.spawnedThisConnection = spawnedThisConnection;
    rendering.render(*screen, simulation, now);
    ui.render(*screen, *this, now);
    cp5::flush();
    bool preventRightClick = config::uiPreventRightClick && !ui.capturesKeys();
    if (preventRightClick != rightClickPrevented) {
        rightClickPrevented = preventRightClick;
        EM_ASM({ window.preventRightClick = !!$0; }, preventRightClick);
    }
}

