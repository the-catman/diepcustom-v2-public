#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "Const.h"
#include "GameRendering.h"
#include "cp5/Context.h"
#include "sim/Simulation.h"

class Game;

// Installs window.input (execute / get_convar / set_convar / print_convar_help).
void installConsoleApi();

class GameUI {
public:
    void render(cp5::Context& ctx, Game& game, double now);

    bool onKeyDown(int keyCode, Game& game);
    bool onMouseDown(Game& game);
    bool onMouseUp(Game& game);
    bool onWheel(double notches); // positive = scroll down

    // Runs a console line (';' / newline separated commands) and returns its output.
    std::string execute(Game& game, const std::string& line);
    void toggleConsole();

    void addNotification(const std::string& text, uint32_t color, double timeMs, const std::string& id, double now);
    void onNewConnection();
    // A text field is up (the DOM input owns typing and its default key actions).
    bool typing() const { return config::renUi && (spawnAlpha > 0.7 || consoleOpen); }
    // The open console takes the keyboard: game keys and mouse buttons read as up.
    bool capturesKeys() const { return config::renUi && consoleOpen; }
    // The topmost UI element under the cursor blocks fire and right click.
    bool blocksFire() const { return hoverBlocks; }

private:
    struct Notification {
        std::string text;
        std::string id;
        uint32_t color = 0;
        double created = 0;
        double expiry = 0; // 0 = never expires
    };
    // Every UI element drawn this frame, in draw order. The topmost one under the cursor sets
    // the cursor, blocks fire and takes clicks; action 0 is inert. Scoreboard rows aren't listed.
    enum Cursor { CURSOR_DEFAULT = 1, CURSOR_POINTER = 2, CURSOR_TEXT = 3 };
    struct HitBox {
        double x, y, w, h;
        int action;
        int arg;
        int cursor = CURSOR_DEFAULT;
    };
    // col/row freeze at first appearance so departing slots don't reflow the grid.
    struct UpgradeSlot {
        int tankId = -1;
        float fade = 0;
        int col = -1;
        int row = -1;
        uint32_t color = 0;
        GameRendering::TankIcon icon;
    };

    float spawnAlpha = 0;
    bool spawnTarget = false;
    bool deathTarget = false;
    // The death screen's target before the 500ms delay; holds the stat panel open.
    bool awaitingContinue = false;
    double screensAfter = 0;
    bool enterPressed = false;
    float deathAlpha = 0;
    GameRendering::TankIcon deathIcon;
    float dimAlpha = 0;
    float stackAlpha = 0;
    float waitAlpha = 0;
    float startAlpha = 0;
    std::string waitSub;
    std::string startText;
    float changelogAlpha = 0;
    float partyAlpha = 0;
    bool changelogDone = false;
    bool changelogLoaded = false;
    std::vector<std::string> changelog;
    // One per achievement: fade, list slot and cached card.
    struct AchievementCard {
        double unlockedAt = 0; // 0 = never, or already unlocked when the list was built
        double fade = 0;
        double row = 0;        // eased list offset, 60 per listed card above
        bool placed = false;   // the row snaps instead of easing on its first frame
        bool unlocked = false;
        bool hasProgress = false;
        double progress = 0;
        std::unique_ptr<cp5::Context> canvas;
        int canvasColor = -2;  // palette index the canvas was drawn with (-1 = locked)
        double canvasUnit = 0;
        double cardW = 0;
    };
    std::vector<AchievementCard> achievementCards;
    bool achievementsBrowsing = false; // set without a player, cleared once the list fades out
    double achievementScroll = 0;
    float achievementScrollVel = 0;
    double achievementListH = 0;       // eased height of the listed cards
    // Bottom of the top-center gamemode stack in ui units; notifications anchor below it.
    double stackBottomU = 24;
    std::string nameInput;
    std::string nameFieldSaved;
    bool nameLoaded = false;
    std::vector<Notification> notifications;
    std::vector<HitBox> hitBoxes;
    void addButton(double x, double y, double w, double h, int action, int arg, bool enabled);
    const HitBox* hitAt(double x, double y) const;
    void updateHover(double mx, double my);
    bool hoverBlocks = false;
    int cursor = 0;
    float connectAlpha = 0;
    bool loadingHidden = false;

    float statsOpen = 0;
    bool statsLatch = false;
    bool lastUHeld = false;
    bool queuedSinceUDown = false;
    bool hadPlayer = false;

    bool consoleOpen = false;
    std::string consoleInput;
    // Output of the last executed line, one entry per '\n'-separated line.
    std::vector<std::string> consoleLines;
    // Wheel scroll with momentum; springs back when past either end.
    float consoleScroll = 0;
    float consoleScrollVel = 0;
    double wheelAccum = 0;

    // Geometry for the overlaid DOM input, stashed in device px by the active field.
    int textFieldActive = 0; // 0 none, 1 console, 2 name
    double tfX = 0, tfY = 0, tfW = 0, tfH = 0, tfFont = 0;
    uint32_t tfColor = 0;
    bool tfCentered = false;
    bool tfOutline = false;

    // Press latches the target button; the action fires on release over it.
    bool pressActive = false;
    int pressedAction = 0;
    int pressedArg = 0;
    bool pressedButton(int action, int arg) const {
        return pressActive && pressedAction == action && pressedArg == arg;
    }

    std::vector<UpgradeSlot> upgradeSlots;
    float upgradeWidthAnim = 0;
    float upgradeBottomAnim = 0;
    float upgradeLastFade = 0;
    bool upgradesIgnored = false;
    int ignoreUntilLevel = 0;

    // Class tree (hold Y): built once from tankdef 0's upgrades. Leaves share the circle
    // equally and parents span their children.
    struct TreeNode {
        int tankId = 0;
        int level = 0;
        int sibling = 0; // index among the parent's children, picks the palette color
        double start = 0, end = 0;
        std::vector<int> children;
        GameRendering::TankIcon icon;
    };
    std::vector<TreeNode> treeNodes; // [0] is the root
    // A ring sector from level inner to outer; node -1 is a grey connector. index is its place
    // in the draw order, which staggers the icon re-renders.
    struct TreeSector {
        int node;
        double start, end;
        int inner, outer;
        int index;
    };
    // Sectors sharing an inner level grow, spin and fade together, so each ring is drawn once
    // into its own canvas (full size, no spin) and blitted, the icons drawn over it.
    struct TreeRing {
        int inner = 0;
        std::vector<TreeSector> sectors;
        std::unique_ptr<cp5::Context> canvas;
    };
    std::vector<TreeRing> treeRings; // deepest first
    // What the ring canvases were drawn with.
    double treeRingRadius = 0;
    uint32_t treeRingPalette[8] = {};
    bool treeBuilt = false;
    float treeFade = 0;
    int treeFrame = 0;

    std::unique_ptr<cp5::Context> hudLayer;
    std::unique_ptr<cp5::Context> upgradeLayer;
    std::unique_ptr<cp5::Context> stackLayer;

    void renderSpawnScreen(cp5::Context& ctx, Game& game, double now, double ui);
    void renderDeathScreen(cp5::Context& ctx, Game& game, double now, double ui);
    void renderGamemodeStack(cp5::Context& ctx, Game& game, double ui);
    void renderCountdown(cp5::Context& ctx, Game& game, double now, double ui, bool lobby);
    void renderChangelog(cp5::Context& ctx, Game& game, double ui, bool hasPlayer);
    void renderPartyButton(cp5::Context& ctx, Game& game, double ui, bool hasPlayer, double mx, double my);
    void renderAchievements(cp5::Context& ctx, Game& game, double now, double ui, bool hasPlayer, double mx);
    void drawAchievementCard(AchievementCard& card, int index, Game& game, double ui);
    void renderNotifications(cp5::Context& ctx, Game& game, double now, double ui);
    void renderStats(cp5::Context& ctx, Game& game, double ui, double mx, double my);
    void renderUpgrades(cp5::Context& ctx, Game& game, double now, double ui, double mx, double my);
    int buildTreeNode(int tankId, int sibling, int depth);
    void renderClassTree(cp5::Context& ctx, Game& game, double now, bool hasPlayer);
    void drawTreeRing(TreeRing& ring, double radius, double lineWidth);
    void renderConnecting(cp5::Context& ctx, Game& game, double ui);
    void renderConsole(cp5::Context& ctx, Game& game, double ui, double now);
    void drawButton(cp5::Context& ctx, double x, double y, double w, double h, uint32_t rgb,
                    bool enabled, bool hover, bool pressed, const std::string& label, double ui,
                    uint32_t borderColor = 0x555555, double borderWidth = 10);
    void queueStat(Game& game, int statId, bool toMax);
    void applyStatUpgrade(Game& game, int statId, int targetLevel);
    void statAction(Game& game, int statId);
    void runCommand(Game& game, const std::string& command, std::string& out);
    void renderFps(cp5::Context& ctx, Game& game, double ui);
    void syncTextInput();
    void submitSpawn(Game& game);
    void padInput(Game& game, bool hasPlayer);
};
