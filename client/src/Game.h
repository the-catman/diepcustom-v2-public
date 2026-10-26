#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <memory>

#include "GameRendering.h"
#include "GameUI.h"
#include "cp5/Context.h"
#include "net/GameConnection.h"
#include "sim/Simulation.h"

// keyCodes; mouse buttons are button + 1.
namespace Key {
    constexpr int Mouse1 = 1, Mouse3 = 3;
    constexpr int Tab = 9, Enter = 13, Shift = 16, Escape = 27, Space = 32, Home = 36;
    constexpr int Left = 37, Up = 38, Right = 39, Down = 40;
    constexpr int Digit1 = 49, Numpad1 = 97;
    constexpr int A = 65, C = 67, D = 68, E = 69, H = 72, I = 73, K = 75, L = 76, M = 77, O = 79;
    constexpr int S = 83, U = 85, W = 87, Y = 89;
    constexpr int F2 = 113;
    constexpr int SemicolonFirefox = 59, Semicolon = 186, Backslash = 220;
}

class Game {
public:
    static Game& get();

    void boot(const std::string& gamemode);
    void frame(double now);

    GameConnection connection;
    Simulation simulation;
    GameRendering rendering;
    GameUI ui;
    std::unique_ptr<cp5::Context> screen;

    double mouseScreenX = 0;
    double mouseScreenY = 0;
    bool accepted = false;
    // Held key by keyCode; reads as up while the console has the keyboard.
    bool keyHeld(int key) const;
    // Gamepad 0 with the standard mapping, polled once a frame. While it is connected and
    // enabled (I toggles) it replaces mouse/keyboard fire, aim and movement.
    bool gamepadEnabled = true;
    bool padConnected = false;
    bool padHeld[16] = {};
    bool padPressed[16] = {};
    bool padActive() const { return padConnected && gamepadEnabled; }
    bool autoFire = false;
    bool autoSpin = false;
    // The ping line, toggled with L.
    bool showPing = true;
    bool spawnedThisConnection = false;
    // Set every frame the player lives, kept through a death while the camera shows death
    // stats, cleared without them or by continuing from the death screen.
    bool deathPending = false;
    int playersOnline = -1;
    // Highest access level any accept (0x7) has granted this page. Server commands are
    // registered up to it and never dropped.
    int commandLevel = -1;
    std::string serverHost;
    std::string gamemode;
    // Party bytes: read from the URL hash before each connect (sent in Init), then replaced by
    // the team's code from 0x6, which also enables "Copy party link" for this connection.
    std::string party;
    bool partyReceived = false;
    std::string partyLink() const;
    // A link's server id waits on the server list before connecting, with "Joining party..."
    // up meanwhile.
    std::string partyServer;
    bool joiningParty = false;

    double pingAverage() const;
    void switchGamemode(const std::string& mode);
    void reconnect();

    void sendSpawn(const std::string& name);
    void sendRespawn();
    void sendStatUpgrade(int statId, int targetLevel);
    void sendTankUpgrade(int tankId);
    void sendTakeTank();
    void sendCommand(const std::string& cmd, const std::vector<std::string>& args);

    void toggleAutoFire();
    void toggleAutoSpin();

private:
    double lastPingSent = -1e9;
    double lastInputSent = -1e9;
    bool pongReceived = true;
    bool keysHeld[256] = {};
    float padAxes[4] = {};                // smoothed (old + new) / 2 each poll
    float padAimX = 0.1f, padAimY = 0.1f; // last right-stick direction (starts at 0.1)
    double padNoticeAt = -1;              // last "Using Gamepad" notice
    void pollGamepad();
    void toggleGamepad();
    bool padMove(float& x, float& y) const;
    bool fireLatch = false; // fire seen on any frame since the last input packet
    bool invincibilityLatch = false;
    bool switchTankLatch = false;
    double spinPhase = 0;
    // Reconnect backoff in frames: a close arms the countdown with the backoff, which then
    // grows to 2b+20; every Update resets it to 1.
    int reconnectCountdown = 0;
    int reconnectBackoff = 1;
    std::vector<double> pingSamples;
    std::string wsHost;
    bool rightClickPrevented = false;

    std::string socketUrl() const;
    void readPartyHash();
    void openSocket();
    void connectSocket();
    bool gamemodeChecked = false;

    bool mouseWorld(double now, double& mx, double& my);

    void onOpen();
    void onClose();
    void onData(const uint8_t* data, size_t len);
    void setKey(int key, bool down);
    bool firing() const;
    uint32_t heldFlags() const;
    void sendInputs(double now);
    void predictTick(double now);
    void wallKicks(double now);
    void settleOwnPosition(double now);

    friend struct InputCallbacks;
};
