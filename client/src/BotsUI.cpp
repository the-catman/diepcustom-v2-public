#include "GameUI.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <emscripten/emscripten.h>

#include "BotsProtocol.h"
#include "Const.h"
#include "Game.h"
#include "TankDefs.h"
#include "UIHelpers.h"

// The setup, kept for the next visit.
EM_JS(void, bots_setup_load, (char* out, int max), {
    stringToUTF8(window.localStorage.getItem("bots_setup") || "", out, max);
});

EM_JS(void, bots_setup_save, (const char* text), {
    window.localStorage.setItem("bots_setup", UTF8ToString(text));
});

EM_JS(void, bots_builds_load, (char* out, int max), {
    stringToUTF8(window.localStorage.getItem("bots_builds") || "", out, max);
});

EM_JS(void, bots_builds_save, (const char* text), {
    window.localStorage.setItem("bots_builds", UTF8ToString(text));
});

namespace {
    struct Format {
        int n0, n1;
        const char* label;
    };
    const Format FORMATS[4] = {{1, 1, "1v1"}, {2, 1, "2v1"}, {1, 2, "1v2"}, {2, 2, "2v2"}};

    constexpr uint32_t TILE = 0xababab; // its pastel is the arena's background
    constexpr uint32_t GREEN = 0x00e16e;
    constexpr uint32_t GREY = 0x777777;
    constexpr uint32_t YOU = 0xffff90;

    // Seat card layout in ui units from the screen's anchor: one column a side, a row per seat.
    constexpr double COLUMN_X = 205, CARD_W = 362, CARD_H = 100, CARD_TOP = -80, ROW_STEP = 120;

    double cardLeft(int side, double cx, double ui) { return cx + ((side ? COLUMN_X : -COLUMN_X) - CARD_W / 2) * ui; }
    double cardTop(int row, double cy, double ui) { return cy + (CARD_TOP + row * ROW_STEP) * ui; }
    // Below the last row of cards; what comes after moves with it.
    double cardsBottom(int rows, double cy, double ui) { return cardTop(rows - 1, cy, ui) + CARD_H * ui; }

    int sideColor(int side) { return side ? Color::TeamRed : Color::TeamBlue; }

    // Text with its middle at y, starting at (align -1), centered on (0) or ending at (1) x.
    void text(cp5::Context& ctx, GameRendering& r, const std::string& s, double x, double y, double size, uint32_t fill,
              int align = 0) {
        if (s.empty()) return;
        GameRendering::TextBox box = r.textBox(ctx, s, size);
        double left = align < 0 ? x - box.margin : align > 0 ? x - box.w + box.margin : x - (double)(box.w / 2);
        r.drawTextBox(ctx, s, box, std::round(left), std::round(y - (double)(box.h / 2)), size, fill);
    }

    std::string clock(int seconds) {
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%d:%02d", seconds / 60, seconds % 60);
        return buf;
    }

    std::string botName(const Game& game, int who) {
        if (who <= 0) return "Player";
        if (who - 1 < (int)game.botsLists.bots.size()) return game.botsLists.bots[(size_t)who - 1].name;
        return "Bot";
    }

    const std::vector<int>& tanksFor(const Game& game, int who) {
        static const std::vector<int> none;
        const bots::Lists& lists = game.botsLists;
        if (who <= 0) return lists.playerTanks;
        return who - 1 < (int)lists.bots.size() ? lists.bots[(size_t)who - 1].tanks : none;
    }

    // Slots in seat order for a format: blue's rows, then red's.
    int slotOf(const Format& f, int seat) { return seat < f.n0 ? seat : 2 + seat - f.n0; }

    // The creator takes the first Player seat; the others are for whoever joins by link.
    template <class Slots> int ownSlot(const Format& f, const Slots& slots) {
        for (int seat = 0; seat < f.n0 + f.n1; ++seat) {
            if (slots[slotOf(f, seat)].who == 0) return slotOf(f, seat);
        }
        return -1;
    }

    bots::Build usualBuild(const Game& game, int tank) {
        const bots::Lists& lists = game.botsLists;
        for (size_t i = 0; i < lists.playerTanks.size() && i < lists.playerBuilds.size(); ++i) {
            if (lists.playerTanks[i] == tank) return lists.playerBuilds[i];
        }
        return {};
    }

    // The tank of the room seat that is ours, or -1.
    int myTank(const bots::Room& room) {
        if (room.you >= (int)room.seats.size()) return -1;
        const bots::Seat& me = room.seats[(size_t)room.you];
        return me.who == 0 ? me.tank : -1;
    }

    // Our own seat's tile: its tank sits higher, over a Build button along the bottom.
    constexpr double BUILD_STRIP_H = 28, BUILD_STRIP_MARGIN = 8;
}

void GameUI::loadBotsSetup() {
    if (botsSetupLoaded) return;
    botsSetupLoaded = true;
    for (int i = 1; i < 4; ++i) botsSetup.slots[i].who = 1;
    char buf[1024] = {0};
    bots_setup_load(buf, sizeof(buf));
    std::string all = buf;
    std::vector<std::string> lines;
    for (size_t at = 0; at < all.size();) {
        size_t end = all.find('\n', at);
        if (end == std::string::npos) end = all.size();
        lines.push_back(all.substr(at, end - at));
        at = end + 1;
    }
    if (lines.size() < 5) return;
    int format = 0, best = 0;
    if (std::sscanf(lines[0].c_str(), "%d %d", &format, &best) != 2) return;
    botsSetup.format = std::min(3, std::max(0, format));
    botsSetup.best = best != 0;
    for (int i = 0; i < 4; ++i) {
        BotsSlot& slot = botsSetup.slots[i];
        int who = 0, tank = -1, used = 0;
        if (std::sscanf(lines[(size_t)i + 1].c_str(), "%d %d %n", &who, &tank, &used) < 2) continue;
        slot.who = std::max(0, who);
        slot.tank = tank;
        slot.bot = used > 0 ? lines[(size_t)i + 1].substr((size_t)used) : "";
    }
}

// format best, then per slot: who tank bot-name.
void GameUI::saveBotsSetup() const {
    std::string out = std::to_string(botsSetup.format) + ' ' + (botsSetup.best ? '1' : '0') + '\n';
    for (const BotsSlot& slot : botsSetup.slots) {
        std::string name = slot.bot;
        name.erase(std::remove(name.begin(), name.end(), '\n'), name.end());
        out += std::to_string(slot.who) + ' ' + std::to_string(slot.tank) + ' ' + name + '\n';
    }
    bots_setup_save(out.c_str());
}

// Points each slot back at its bot by name and at a tank that bot (or a player) can take.
void GameUI::fitBotsSetup(const Game& game) {
    const std::vector<bots::Lists::Bot>& list = game.botsLists.bots;
    if (list.empty()) return;
    for (BotsSlot& slot : botsSetup.slots) {
        if (slot.who > 0) {
            int found = -1;
            for (size_t i = 0; i < list.size(); ++i) if (list[i].name == slot.bot) { found = (int)i; break; }
            if (found >= 0) slot.who = found + 1;
            else if (slot.who > (int)list.size()) slot.who = 1;
            slot.bot = list[(size_t)slot.who - 1].name;
        }
        const std::vector<int>& tanks = tanksFor(game, slot.who);
        if (std::find(tanks.begin(), tanks.end(), slot.tank) == tanks.end()) slot.tank = tanks.empty() ? -1 : tanks[0];
    }
}

// A line a tank: its id, then its 8 stats in server order.
void GameUI::loadBotsBuilds() {
    if (botsBuildsLoaded) return;
    botsBuildsLoaded = true;
    char buf[4096] = {0};
    bots_builds_load(buf, sizeof(buf));
    for (const char* at = buf; *at;) {
        int tank = -1;
        bots::Build b{};
        if (std::sscanf(at, "%d %d %d %d %d %d %d %d %d", &tank, &b[0], &b[1], &b[2], &b[3], &b[4], &b[5], &b[6], &b[7]) == 9 &&
            tank >= 0)
            botsBuilds[tank] = b;
        const char* end = std::strchr(at, '\n');
        if (!end) break;
        at = end + 1;
    }
}

void GameUI::saveBotsBuilds() const {
    std::string out;
    for (const auto& [tank, build] : botsBuilds) {
        out += std::to_string(tank);
        for (int x : build) out += ' ' + std::to_string(x);
        out += '\n';
    }
    bots_builds_save(out.c_str());
}

// A saved build the tank's caps no longer allow gives way to the usual one.
bots::Build GameUI::botsBuild(const Game& game, int tank) const {
    auto it = botsBuilds.find(tank);
    if (it == botsBuilds.end()) return usualBuild(game, tank);
    int points = 0;
    for (int i = 0; i < 8; ++i) {
        if (it->second[i] < 0 || it->second[i] > tankdefs::statMax(tank, i)) return usualBuild(game, tank);
        points += it->second[i];
    }
    return points <= bots::BUILD_POINTS ? it->second : usualBuild(game, tank);
}

std::optional<bots::Build> GameUI::botsOwnBuild(const Game& game, int tank) const {
    bots::Build build = botsBuild(game, tank);
    if (build == usualBuild(game, tank)) return std::nullopt;
    return build;
}

void GameUI::setBotsBuild(const Game& game, int tank, const bots::Build& build) {
    if (build == usualBuild(game, tank)) botsBuilds.erase(tank);
    else botsBuilds[tank] = build;
    saveBotsBuilds();
}

// A seat already ready in the room sends the build again; one not yet ready sends it with Ready.
void GameUI::closeBotsBuild(Game& game) {
    if (botsBuildTank < 0) return;
    const bots::Room& room = game.botsRoom;
    if (botsBuildScreen == 1 && myTank(room) == botsBuildTank) {
        const bots::Seat& me = room.seats[(size_t)room.you];
        if (me.state & bots::SeatState::ready) game.sendBotsReady({me.name, botsOwnBuild(game, me.tank)});
    }
    botsBuildTank = -1;
}

void GameUI::buildStrip(cp5::Context& ctx, double left, double top, uint32_t rgb, int tank, bool live, double ui) {
    double m = BUILD_STRIP_MARGIN * ui, h = BUILD_STRIP_H * ui, w = CARD_H * ui - 2 * m;
    double x = left + m, y = top + CARD_H * ui - m - h;
    bool over = botsMx >= x && botsMx <= x + w && botsMy >= y && botsMy <= y + h;
    drawButton(ctx, x, y, w, h, rgb, live, over, live && over && pressedButton(BOTS_BUILD_OPEN, tank), "Build", ui,
               0x333333, 4);
    addButton(x, y, w, h, BOTS_BUILD_OPEN, tank, live);
}

void GameUI::botsButton(cp5::Context& ctx, double x, double y, double w, double h, uint32_t rgb, const std::string& label,
                        int action, int arg, bool enabled, double ui, bool selected) {
    bool over = botsMx >= x && botsMx <= x + w && botsMy >= y && botsMy <= y + h;
    bool pressed = selected || (enabled && over && pressedButton(action, arg));
    drawButton(ctx, x, y, w, h, rgb, enabled && !selected, over, pressed, label, ui, 0x333333, 7.5);
    addButton(x, y, w, h, action, arg, enabled && !selected);
}

// Once a frame before anything draws: which screen is up, and the fades.
void GameUI::updateBots(Game& game) {
    loadName();
    loadBotsSetup();
    loadBotsBuilds();
    if (game.botsListsReceived) fitBotsSetup(game);

    const bots::Room& room = game.botsRoom;
    if (room.phase != botsPhaseSeen) {
        botsPhaseSeen = room.phase;
        if (room.phase != bots::None) botsStartHeld = 0;
        if (room.phase == bots::Over) botsVoted = false;
        if (room.phase == bots::Playing) botsClockShown = 0;
    }

    float dt = game.rendering.dt();
    botsScreen = !game.accepted ? -1
               : room.phase == bots::None ? 0
               : room.phase <= bots::Countdown ? 1
               : room.phase == bots::Playing ? 2 : 3;
    for (int i = 0; i < 4; ++i) {
        bool on = i == botsScreen;
        botsAlpha[i] = approach(botsAlpha[i], on ? 1.0f : 0.0f, dt, on ? 0.05f : 0.1f);
    }
    if (botsScreen > 0) {
        botsShown[botsScreen] = room;
        botsShownAt[botsScreen] = game.botsRoomAt;
    }
    if (botsScreen != 0) botsPickerSlot = -1;
    if (botsScreen != botsBuildScreen) botsBuildTank = -1;
    bool picking = botsPickerSlot >= 0, building = botsBuildTank >= 0;
    botsPickerAlpha = approach(botsPickerAlpha, picking ? 1.0f : 0.0f, dt, picking ? 0.1f : 0.15f);
    if (botsPickerAlpha < 0.01 && !picking) botsPickerIcons.clear();
    botsBuildAlpha = approach(botsBuildAlpha, building ? 1.0f : 0.0f, dt, building ? 0.1f : 0.15f);
    double dpr = cp5::pixelRatio();
    botsMx = game.mouseScreenX * dpr;
    botsMy = game.mouseScreenY * dpr;
}

void GameUI::renderBots(cp5::Context& ctx, Game& game, double now, double ui, bool confirm) {
    botsNameUp = false;
    bool overlay = botsOverlay();
    if (confirm && botsBuildTank >= 0) closeBotsBuild(game);
    if (botsAlpha[0] > 0.01) renderBotsSetup(ctx, game, ui, confirm && botsScreen == 0 && !overlay);
    if (botsAlpha[1] > 0.01) renderBotsRoom(ctx, game, now, ui, confirm && botsScreen == 1 && !overlay);
    if (botsAlpha[3] > 0.01) renderBotsResult(ctx, game, ui);
    if (botsPickerAlpha > 0.01) renderBotsPicker(ctx, game, ui);
    if (botsBuildAlpha > 0.01) renderBotsBuild(ctx, game, ui);
}

// ---- Setup ----

void GameUI::renderBotsSetup(cp5::Context& ctx, Game& game, double ui, bool confirm) {
    GameRendering& r = game.rendering;
    const bots::Lists& lists = game.botsLists;
    float alpha = botsAlpha[0];
    bool live = alpha > 0.9 && botsScreen == 0 && !botsOverlay();
    double cx = cp5::canvasWidth() / 2;
    double cy = slideAnchor(alpha, cp5::canvasHeight(), ui);
    const Format& format = FORMATS[botsSetup.format];

    bool online = game.botsListsReceived && lists.online && !lists.bots.empty();
    bool tanksSet = true;
    for (int seat = 0; seat < format.n0 + format.n1; ++seat) tanksSet &= botsSetup.slots[slotOf(format, seat)].tank >= 0;
    bool canStart = online && tanksSet && emscripten_get_now() >= botsStartHeld;
    if (confirm && canStart) botsStart(game);

    ctx.save();
    ctx.setAlpha(alpha * (1 - std::max(botsPickerAlpha, botsBuildAlpha)));

    r.drawLabel(ctx, "This is the tale of...", cx, cy - 275 * ui, 24 * ui, 0xffffff);
    bool nameActive = alpha > 0.7 && !consoleOpen && !botsOverlay();
    drawNameBox(ctx, cx, cy - 230 * ui, ui, nameActive);
    botsNameUp |= nameActive;

    double fw = 90 * ui, fh = 32 * ui;
    for (int i = 0; i < 4; ++i) {
        double fx = cx + ((i - 1.5) * 100 - 45) * ui;
        botsButton(ctx, fx, cy - 160 * ui - fh / 2, fw, fh, teamColorHex(i), FORMATS[i].label, BOTS_FORMAT, i, live,
                   ui, i == botsSetup.format);
    }

    text(ctx, r, "Blue", cx - COLUMN_X * ui, cy - 105 * ui, 26 * ui, colorHex(Color::TeamBlue));
    text(ctx, r, "Red", cx + COLUMN_X * ui, cy - 105 * ui, 26 * ui, colorHex(Color::TeamRed));
    text(ctx, r, "vs", cx, cy + (CARD_TOP + CARD_H / 2) * ui, 24 * ui, 0xffffff);
    double bottom = cardsBottom(std::max(format.n0, format.n1), cy, ui);
    int own = ownSlot(format, botsSetup.slots);

    for (int side = 0; side < 2; ++side) {
        int rows = side ? format.n1 : format.n0;
        uint32_t color = colorHex(sideColor(side));
        for (int row = 0; row < rows; ++row) {
            int s = side * 2 + row;
            BotsSlot& slot = botsSetup.slots[s];
            double left = cardLeft(side, cx, ui), top = cardTop(row, cy, ui);
            double box = CARD_H * ui;
            bool building = s == own && slot.tank >= 0;
            botsButton(ctx, left, top, box, box, TILE, "", BOTS_PICKER_OPEN, s, live && slot.tank >= 0, ui);
            if (slot.tank >= 0)
                r.drawTankIcon(ctx, botsSlotIcons[s], slot.tank, left + box / 2, top + box * (building ? 0.36 : 0.5),
                               side ? M_PI : 0, 0.4 * ui, true, true, sideColor(side));
            if (building) buildStrip(ctx, left, top, color, slot.tank, live, ui);

            // Two pickers: [<] label [>], who on top and the tank under it.
            double rl = left + 112 * ui, aw = 34 * ui, lw = 170 * ui;
            std::string who = slot.who > 0 && !slot.bot.empty() ? slot.bot : botName(game, slot.who);
            std::string tank = slot.tank >= 0 ? tankdefs::name(slot.tank) : "";
            bool whoLive = live && !lists.bots.empty();
            bool tankLive = live && tanksFor(game, slot.who).size() > 1;
            for (int line = 0; line < 2; ++line) {
                double y = top + (line ? 58 : 8) * ui;
                int prev = line ? BOTS_TANK_PREV : BOTS_WHO_PREV, next = line ? BOTS_TANK_NEXT : BOTS_WHO_NEXT;
                bool on = line ? tankLive : whoLive;
                botsButton(ctx, rl, y, aw, aw, color, "<", prev, s, on, ui);
                botsButton(ctx, rl + aw + 6 * ui, y, lw, aw, color, line ? tank : who, line ? BOTS_PICKER_OPEN : next, s,
                           line ? live && slot.tank >= 0 : on, ui);
                botsButton(ctx, rl + aw + lw + 12 * ui, y, aw, aw, color, ">", next, s, on, ui);
            }
        }
    }

    // "Bots play their best move": a box with a tick, its label beside it.
    {
        const char* label = "Bots play their best move";
        double size = 18 * ui, bw = 28 * ui;
        GameRendering::TextBox box = r.textBox(ctx, label, size);
        double total = bw + 10 * ui + (box.w - 2 * box.margin);
        double bx = cx - total / 2, by = bottom + 45 * ui - bw / 2;
        bool over = botsMx >= bx && botsMx <= bx + total && botsMy >= by && botsMy <= by + bw;
        drawButton(ctx, bx, by, bw, bw, botsSetup.best ? GREEN : GREY, live, over,
                   over && pressedButton(BOTS_BEST, 0), "", ui, 0x333333, 7.5);
        if (botsSetup.best) {
            ctx.save();
            ctx.beginPath();
            ctx.moveTo(bx + bw * 0.24, by + bw * 0.52);
            ctx.lineTo(bx + bw * 0.43, by + bw * 0.7);
            ctx.lineTo(bx + bw * 0.76, by + bw * 0.3);
            ctx.setLineCap(cp5::LineCap::round);
            ctx.setLineJoin(cp5::LineJoin::round);
            ctx.setStrokeColor(0x000000);
            ctx.setLineWidth(bw * 0.28);
            ctx.stroke();
            ctx.setStrokeColor(0xffffff);
            ctx.setLineWidth(bw * 0.14);
            ctx.stroke();
            ctx.restore();
        }
        text(ctx, r, label, bx + bw + 10 * ui, by + bw / 2, size, 0xffffff, -1);
        addButton(bx, by, total, bw, BOTS_BEST, 0, live);
    }

    double sw = 220 * ui, sh = 50 * ui, sy = bottom + 110 * ui;
    botsButton(ctx, cx - sw / 2, sy - sh / 2, sw, sh, GREEN, "Start", BOTS_START, 0, live && canStart, ui);
    if (game.botsListsReceived && !online) text(ctx, r, "Bots are offline", cx, sy + 50 * ui, 18 * ui, 0xffffff);
    else if (canStart) text(ctx, r, "(press enter to start)", cx, sy + 45 * ui, 14 * ui, 0xffffff);
    ctx.restore();
}

void GameUI::botsStart(Game& game) {
    const Format& format = FORMATS[botsSetup.format];
    bots::Create create;
    create.deterministic = botsSetup.best;
    create.n0 = format.n0;
    create.n1 = format.n1;
    for (int seat = 0; seat < format.n0 + format.n1; ++seat) {
        const BotsSlot& slot = botsSetup.slots[slotOf(format, seat)];
        bots::Seat s;
        s.who = slot.who;
        s.tank = slot.tank;
        create.seats.push_back(s);
    }
    int own = ownSlot(format, botsSetup.slots);
    if (own >= 0) create.build = botsOwnBuild(game, botsSetup.slots[own].tank);
    create.name = takeName();
    game.sendBotsCreate(create);
    botsStartHeld = emscripten_get_now() + 3000;
}

// A grid of every tank the slot can take; a click picks one, anywhere else closes it.
void GameUI::renderBotsPicker(cp5::Context& ctx, Game& game, double ui) {
    GameRendering& r = game.rendering;
    int s = botsPickerShown;
    const BotsSlot& slot = botsSetup.slots[s];
    const std::vector<int>& tanks = tanksFor(game, slot.who);
    int n = (int)tanks.size();
    float alpha = botsPickerAlpha;
    bool live = alpha > 0.9 && botsPickerSlot >= 0;
    double w = cp5::canvasWidth(), h = cp5::canvasHeight();
    double cx = w / 2, cy = h / 2;

    ctx.save();
    ctx.setAlpha(alpha * 0.5);
    ctx.setFillColor(0x000000);
    ctx.fillRect(0, 0, w, h);
    ctx.setAlpha(alpha);
    addButton(0, 0, w, h, BOTS_PICKER_CLOSE, 0, live);

    double box = 90 * ui, step = 104 * ui;
    int maxCols = std::max(1, (int)((w - 120 * ui) / step));
    int cols = std::max(1, std::min({n, maxCols, (int)std::ceil(std::sqrt(n * 2.0))}));
    int rows = (n + cols - 1) / cols;
    double gridH = rows * step - (step - box), room = h - 300 * ui;
    double viewH = std::min(gridH, room);
    if (gridH > room) {
        if (!consoleOpen) {
            botsPickerScroll += wheelAccum * 60 * ui;
            wheelAccum = 0;
        }
        botsPickerScroll = std::min(std::max(0.0, botsPickerScroll), gridH - room);
    } else {
        botsPickerScroll = 0;
    }
    double top = cy - viewH / 2;

    text(ctx, r, "Choose a tank", cx, top - 45 * ui, 28 * ui, 0xffffff);

    if ((int)botsPickerIcons.size() != n) botsPickerIcons.resize((size_t)n);
    ctx.save();
    ctx.beginPath();
    ctx.rect(0, top - 10 * ui, w, viewH + 20 * ui);
    ctx.clip();
    double left = cx - (cols * step - (step - box)) / 2;
    for (int i = 0; i < n; ++i) {
        int col = i % cols, row = i / cols;
        double x = left + col * step, y = top + row * step - botsPickerScroll;
        if (y + box < top - 10 * ui || y > top + viewH + 10 * ui) continue;
        bool visible = y >= top - 1 && y + box <= top + viewH + 1;
        botsButton(ctx, x, y, box, box, teamColorHex(i & 7), "", BOTS_PICK, tanks[(size_t)i], live && visible, ui,
                   tanks[(size_t)i] == slot.tank);
        r.drawTankIcon(ctx, botsPickerIcons[(size_t)i], tanks[(size_t)i], x + box / 2, y + box / 2, -M_PI / 4, 0.36 * ui,
                       true, true, sideColor(s / 2));
        const std::string name = tankdefs::name(tanks[(size_t)i]);
        double nameSize = 13 * ui;
        GameRendering::TextBox nameBox = r.textBox(ctx, name, nameSize);
        r.drawTextBox(ctx, name, nameBox, std::round(x + box / 2 - (double)(nameBox.w / 2)),
                      std::round(y + box - nameBox.h),
                      nameSize, 0xffffff);
    }
    ctx.restore();

    double bw = 120 * ui, bh = 32 * ui;
    botsButton(ctx, cx - bw / 2, top + viewH + 30 * ui, bw, bh, GREY, "Cancel", BOTS_PICKER_CLOSE, 0, live, ui);
    ctx.restore();
}

// The tank's stat bars, regen on top as in a match. A click on a segment fills the bar to it, - and + move it by
// one; anywhere else closes it.
void GameUI::renderBotsBuild(cp5::Context& ctx, Game& game, double ui) {
    GameRendering& r = game.rendering;
    int tank = botsBuildShown;
    bots::Build build = botsBuild(game, tank);
    float alpha = botsBuildAlpha;
    bool live = alpha > 0.9 && botsBuildTank >= 0;
    double w = cp5::canvasWidth(), h = cp5::canvasHeight();
    double cx = w / 2, cy = h / 2;

    ctx.save();
    ctx.setAlpha(alpha * 0.5);
    ctx.setFillColor(0x000000);
    ctx.fillRect(0, 0, w, h);
    ctx.setAlpha(alpha);
    addButton(0, 0, w, h, BOTS_BUILD_CLOSE, 0, live);

    std::vector<int> stats;
    int widest = 0, spent = 0;
    for (int i = 7; i >= 0; --i) {
        int max = tankdefs::statMax(tank, i);
        spent += build[i];
        if (max <= 0) continue;
        stats.push_back(i);
        widest = std::max(widest, max);
    }
    double step = 34 * ui, gap = 4 * ui, segH = 22 * ui, border = 5 * ui, rowH = 46 * ui, bw = 32 * ui;
    double barX = cx - widest * step / 2;
    double top = cy - stats.size() * rowH / 2 + 40 * ui;

    r.drawTankIcon(ctx, botsBuildIcon, tank, cx, top - 150 * ui, -M_PI / 4, 0.4 * ui, true, true, Color::TeamBlue);
    text(ctx, r, tankdefs::name(tank), cx, top - 85 * ui, 28 * ui, 0xffffff);
    text(ctx, r, std::to_string(spent) + "/" + std::to_string(bots::BUILD_POINTS) + " points", cx, top - 50 * ui,
         18 * ui, spent < bots::BUILD_POINTS ? 0xffffff : YOU);

    for (size_t k = 0; k < stats.size(); ++k) {
        int i = stats[k], max = tankdefs::statMax(tank, i), level = build[i];
        double y = top + (k + 0.5) * rowH, len = max * step, rad = segH / 2;
        uint32_t color = teamColorHex(i);
        int most = std::min(max, bots::BUILD_POINTS - (spent - level));

        // Where a click on the hovered segment would leave the bar.
        int shown = level;
        bool over = live && botsMx >= barX && botsMx < barX + len && std::abs(botsMy - y) <= rad + border;
        if (over) {
            int to = (int)((botsMx - barX) / step) + 1;
            shown = std::max(0, std::min(to == level ? to - 1 : to, most));
        }

        ctx.save();
        ctx.setLineCap(cp5::LineCap::round);
        ctx.beginPath();
        ctx.moveTo(barX + rad, y);
        ctx.lineTo(barX + len - rad, y);
        ctx.setLineWidth(segH + 2 * border);
        ctx.setStrokeColor(0x000000);
        ctx.stroke();
        ctx.beginPath();
        ctx.arc(barX + rad, y, rad, M_PI / 2, M_PI * 3 / 2);
        ctx.arc(barX + len - rad, y, rad, -M_PI / 2, M_PI / 2);
        ctx.closePath();
        ctx.clip();
        for (int seg = 0; seg < std::max(level, shown); ++seg) {
            ctx.setFillColor(seg < std::min(level, shown) ? color : blendChannel3(color, 0.5f, 0.0f));
            ctx.fillRect(barX + seg * step, y - rad, seg == max - 1 ? step : step - gap, segH);
        }
        ctx.restore();
        for (int seg = 0; seg < max; ++seg)
            addButton(barX + seg * step, y - rad - border, step, segH + 2 * border, BOTS_BUILD_SET, i * 16 + seg + 1, live);
        text(ctx, r, tankdefs::statName(tank, i), barX + len / 2, y, 14 * ui, 0xffffff);

        botsButton(ctx, barX - 14 * ui - bw, y - bw / 2, bw, bw, color, "-", BOTS_BUILD_LESS, i, live && level > 0, ui);
        botsButton(ctx, barX + len + 14 * ui, y - bw / 2, bw, bw, color, "+", BOTS_BUILD_MORE, i, live && level < most, ui);
    }

    bool custom = botsBuilds.count(tank) > 0;
    double by = top + stats.size() * rowH + 30 * ui, bh = 36 * ui, resetW = 190 * ui, doneW = 130 * ui, between = 20 * ui;
    double bx = cx - (resetW + between + doneW) / 2;
    botsButton(ctx, bx, by, resetW, bh, GREY, "Reset to default", BOTS_BUILD_RESET, 0, live && custom, ui);
    botsButton(ctx, bx + resetW + between, by, doneW, bh, GREEN, "Done", BOTS_BUILD_CLOSE, 0, live, ui);
    ctx.restore();
}

// ---- Room: waiting and countdown ----

void GameUI::renderBotsRoom(cp5::Context& ctx, Game& game, double now, double ui, bool confirm) {
    GameRendering& r = game.rendering;
    const bots::Room& room = botsShown[1];
    float alpha = botsAlpha[1];
    bool live = alpha > 0.9 && botsScreen == 1 && !botsOverlay();
    double cx = cp5::canvasWidth() / 2;
    double cy = slideAnchor(alpha, cp5::canvasHeight(), ui);
    int seats = std::min((int)room.seats.size(), bots::MAX_SEATS);
    bool mine = room.you < seats;
    const bots::Seat* me = mine ? &room.seats[(size_t)room.you] : nullptr;
    bool unready = me && (me->state & bots::SeatState::filled) && !(me->state & bots::SeatState::ready);

    int open = 0;
    for (int i = 0; i < seats; ++i) {
        const bots::Seat& seat = room.seats[(size_t)i];
        open += seat.who == 0 && !(seat.state & bots::SeatState::filled);
    }

    ctx.save();
    ctx.setAlpha(alpha * (1 - botsBuildAlpha));

    std::string title, sub;
    if (room.phase == bots::Countdown) {
        double left = std::max(0.0, room.ticks - (now - botsShownAt[1]) / 40.0);
        title = "Game starting in " + std::to_string(std::max(1, (int)std::ceil(left / bots::TICKS_PER_SECOND)));
    } else {
        title = "Waiting for players...";
    }
    if (open > 0) sub = std::to_string(open) + " more player" + (open == 1 ? "" : "s") + " needed";
    else if (room.phase == bots::Waiting) sub = "Waiting for everyone to be ready";
    else sub = std::to_string(room.n0) + 'v' + std::to_string(room.n1);
    if (room.phase == bots::Countdown && room.deterministic) sub += ", bots play their best move";
    r.drawLabel(ctx, title, cx, cy - 250 * ui, 70 * ui, 0xffffff, true, 0x000000, 0.1);
    r.drawLabel(ctx, sub, cx, cy - 187 * ui, 25 * ui, 0xffffff);

    text(ctx, r, "Blue", cx - COLUMN_X * ui, cy - 105 * ui, 26 * ui, colorHex(Color::TeamBlue));
    text(ctx, r, "Red", cx + COLUMN_X * ui, cy - 105 * ui, 26 * ui, colorHex(Color::TeamRed));
    text(ctx, r, "vs", cx, cy + (CARD_TOP + CARD_H / 2) * ui, 24 * ui, 0xffffff);

    for (int i = 0; i < seats; ++i) {
        const bots::Seat& seat = room.seats[(size_t)i];
        int side = i < room.n0 ? 0 : 1, row = side ? i - room.n0 : i;
        if (row > 1) continue;
        double left = cardLeft(side, cx, ui), top = cardTop(row, cy, ui), box = CARD_H * ui;
        bool building = i == room.you && seat.who == 0;
        drawButton(ctx, left, top, box, box, TILE, false, false, false, "", ui, 0x333333, 7.5);
        r.drawTankIcon(ctx, botsRoomIcons[i], seat.tank, left + box / 2, top + box * (building ? 0.36 : 0.5),
                       side ? M_PI : 0, 0.4 * ui, true, true, sideColor(side));
        if (building) buildStrip(ctx, left, top, colorHex(sideColor(side)), seat.tank, live, ui);

        bool filled = seat.state & bots::SeatState::filled;
        std::string name, status;
        uint32_t statusColor = 0xffffff;
        if (seat.who > 0) {
            name = botName(game, seat.who);
            status = "Bot";
        } else if (!filled) {
            name = "Open seat";
            status = "Waiting";
            statusColor = 0xbbbbbb;
        } else {
            name = seat.name.empty() ? "Unnamed tank" : seat.name;
            bool ready = seat.state & bots::SeatState::ready;
            status = ready ? "Ready" : "Not ready";
            statusColor = ready ? colorHex(Color::TeamGreen) : 0xffde43;
        }
        if (i == room.you) name += " (you)";
        double tx = left + 112 * ui;
        text(ctx, r, name, tx, top + 24 * ui, 22 * ui, i == room.you ? YOU : 0xffffff, -1);
        text(ctx, r, tankdefs::name(seat.tank), tx, top + 54 * ui, 16 * ui, 0xffffff, -1);
        text(ctx, r, status, tx, top + 80 * ui, 16 * ui, statusColor, -1);
    }

    // A guest who came by link names themselves and gets ready; the creator shares the link.
    double bottom = cardsBottom(std::min(2, std::max(1, std::max(room.n0, room.n1))), cy, ui);
    bool naming = unready && room.phase == bots::Waiting;
    double by = bottom + (naming ? 140 : 75) * ui, bh = 40 * ui, gap = 20 * ui;
    struct Button { const char* label; uint32_t color; int action; double w; };
    std::vector<Button> buttons;
    std::string linkLabel = now - botsCopiedAt < 1500 ? "Copied!" : "Copy party link";
    if (naming) {
        r.drawLabel(ctx, "This is the tale of...", cx, bottom + 30 * ui, 24 * ui, 0xffffff);
        bool nameActive = alpha > 0.7 && !consoleOpen && !botsOverlay();
        drawNameBox(ctx, cx, bottom + 75 * ui, ui, nameActive);
        botsNameUp |= nameActive;
        buttons.push_back({"Ready", GREEN, BOTS_READY, 160 * ui});
        if (confirm) game.sendBotsReady({takeName(), botsOwnBuild(game, me->tank)});
    }
    if (open > 0 && room.phase == bots::Waiting && !game.party.empty())
        buttons.push_back({nullptr, GREY, BOTS_LINK, 180 * ui});
    buttons.push_back({"Cancel", colorHex(Color::TeamRed), BOTS_LEAVE, 140 * ui});
    double total = -gap;
    for (const Button& b : buttons) total += b.w + gap;
    double bx = cx - total / 2;
    for (const Button& b : buttons) {
        botsButton(ctx, bx, by - bh / 2, b.w, bh, b.color, b.label ? b.label : linkLabel, b.action, 0, live, ui);
        bx += b.w + gap;
    }
    if (naming) text(ctx, r, "(press enter when ready)", cx, by + 40 * ui, 14 * ui, 0xffffff);
    ctx.restore();
}

// ---- Match: the clock and each side's tanks, at the top ----

void GameUI::renderBotsMatch(cp5::Context& ctx, Game& game, double now, double ui, bool hasPlayer) {
    GameRendering& r = game.rendering;
    const bots::Room& room = botsShown[2];
    float alpha = botsAlpha[2];
    int seats = std::min((int)room.seats.size(), bots::MAX_SEATS);
    bool playing = game.botsRoom.phase == bots::Playing;
    // The room says who is alive; the simulation, whether we see our tank (it is gone for a moment
    // whenever a match start resets it).
    bool dead = room.you >= seats || !(room.seats[(size_t)room.you].state & bots::SeatState::alive);
    bool spectating = playing && dead && !hasPlayer;
    float dt = r.dt();
    botsSpectateAlpha = approach(botsSpectateAlpha, spectating ? 1.0f : 0.0f, dt, spectating ? 0.05f : 0.1f);
    notifyShift = (float)(alpha * (70 + 30 * botsSpectateAlpha) * ui);
    if (alpha < 0.01) return;

    if (playing) {
        int seconds = (int)((room.ticks + (now - botsShownAt[2]) / 40.0) / bots::TICKS_PER_SECOND);
        botsClockShown = std::max(botsClockShown, seconds);
    }
    double cx = cp5::canvasWidth() / 2, y = 42 * ui;
    ctx.save();
    ctx.setAlpha(alpha);
    text(ctx, r, clock(botsClockShown), cx, y, 32 * ui, 0xffffff);
    for (int side = 0; side < 2; ++side) {
        int from = side ? room.n0 : 0, to = side ? seats : std::min(room.n0, seats);
        double dir = side ? 1 : -1;
        int left = 0;
        for (int i = from; i < to; ++i) left += (room.seats[(size_t)i].state & bots::SeatState::alive) != 0;
        text(ctx, r, std::to_string(left), cx + dir * 70 * ui, y, 32 * ui, colorHex(sideColor(side)));
        for (int i = from; i < to; ++i) {
            bool alive = room.seats[(size_t)i].state & bots::SeatState::alive;
            ctx.save();
            ctx.scaleAlpha(alive ? 1.0 : 0.3);
            r.drawTankIcon(ctx, botsHudIcons[i], room.seats[(size_t)i].tank, cx + dir * (130 + (i - from) * 60) * ui, y,
                           side ? M_PI : 0, 0.25 * ui, true, true, alive ? sideColor(side) : Color::Fallen);
            ctx.restore();
        }
    }
    ctx.restore();

    if (botsSpectateAlpha > 0.01) {
        ctx.save();
        ctx.setAlpha(alpha * botsSpectateAlpha);
        text(ctx, r, "Spectating", cx, 92 * ui, 24 * ui, 0xffffff);
        ctx.restore();
    }
}

// ---- Result ----

void GameUI::renderBotsResult(cp5::Context& ctx, Game& game, double ui) {
    GameRendering& r = game.rendering;
    const bots::Room& room = botsShown[3];
    const bots::Result& res = game.botsResult;
    float alpha = botsAlpha[3];
    bool live = alpha > 0.9 && botsScreen == 3;
    double cx = cp5::canvasWidth() / 2;
    double cy = slideAnchor(alpha, cp5::canvasHeight(), ui);
    int seats = std::min((int)room.seats.size(), bots::MAX_SEATS);
    bool seated = room.you < seats;

    std::string title = "Match over", sub;
    if (game.botsResultReceived) {
        const char* sides[2] = {"Blue", "Red"};
        int mySide = seated ? (room.you < room.n0 ? 0 : 1) : -1;
        bool draw = res.winner != 0 && res.winner != 1;
        if (draw) title = "Draw";
        else if (mySide < 0) title = std::string(sides[res.winner]) + " wins";
        else title = res.winner == mySide ? "Victory" : "Defeat";
        std::string at = clock((int)(res.ticks / bots::TICKS_PER_SECOND));
        switch (res.reason) {
            case bots::Eliminated:
                sub = draw ? "Everyone was eliminated" : std::string(sides[res.winner]) + " wins in " + at;
                break;
            case bots::TimedOut: sub = "Time ran out at " + at; break;
            case bots::SomeoneLeft: sub = "A player left the match"; break;
            case bots::BotsOffline: sub = "The bots went offline"; break;
        }
    }

    ctx.save();
    ctx.setAlpha(alpha);
    r.drawLabel(ctx, title, cx, cy - 250 * ui, 70 * ui, 0xffffff, true, 0x000000, 0.1);
    r.drawLabel(ctx, sub, cx, cy - 187 * ui, 25 * ui, 0xffffff);

    double iconX = cx - 300 * ui, nameX = cx - 260 * ui, tankX = cx - 20 * ui;
    double killsX = cx + 170 * ui, dealtX = cx + 290 * ui;
    double y = cy - 130 * ui;
    text(ctx, r, "Tank", tankX, y, 16 * ui, 0xffffff, -1);
    text(ctx, r, "Kills", killsX, y, 16 * ui, 0xffffff);
    text(ctx, r, "Damage", dealtX, y, 16 * ui, 0xffffff);
    y += 44 * ui;
    for (int i = 0; i < seats; ++i) {
        const bots::Seat& seat = room.seats[(size_t)i];
        int side = i < room.n0 ? 0 : 1;
        if (i == room.n0) y += 14 * ui;
        r.drawTankIcon(ctx, botsResultIcons[i], seat.tank, iconX, y, side ? M_PI : 0, 0.28 * ui, true, true,
                       sideColor(side));
        std::string name = seat.who > 0 ? botName(game, seat.who) : seat.name.empty() ? "Unnamed tank" : seat.name;
        if (i == room.you) name += " (you)";
        text(ctx, r, name, nameX, y, 22 * ui, i == room.you ? YOU : 0xffffff, -1);
        text(ctx, r, tankdefs::name(seat.tank), tankX, y, 18 * ui, 0xffffff, -1);
        if (game.botsResultReceived && i < (int)res.seats.size()) {
            text(ctx, r, std::to_string(res.seats[(size_t)i].kills), killsX, y, 22 * ui, 0xffffff);
            text(ctx, r, formatCommas(std::lround(res.seats[(size_t)i].dealt / 100.0)), dealtX, y, 22 * ui, 0xffffff);
        }
        y += 46 * ui;
    }

    int humans = 0, votes = 0;
    for (int i = 0; i < seats; ++i) {
        const bots::Seat& seat = room.seats[(size_t)i];
        if (seat.who != 0 || !(seat.state & bots::SeatState::filled)) continue;
        ++humans;
        votes += (seat.state & bots::SeatState::wantsRematch) != 0;
    }
    bool voted = botsVoted || (seated && (room.seats[(size_t)room.you].state & bots::SeatState::wantsRematch));
    std::string rematch = "Rematch";
    if (humans > 1) rematch += " (" + std::to_string(votes) + "/" + std::to_string(humans) + ")";
    double bw = 200 * ui, bh = 44 * ui, gap = 20 * ui, by = y + 40 * ui - bh / 2;
    if (seated) {
        botsButton(ctx, cx - bw - gap / 2, by, bw, bh, GREEN, rematch, BOTS_REMATCH, 0, live, ui, voted);
        botsButton(ctx, cx + gap / 2, by, bw, bh, GREY, "Change setup", BOTS_LEAVE, 0, live, ui);
    } else {
        botsButton(ctx, cx - bw / 2, by, bw, bh, GREY, "Change setup", BOTS_LEAVE, 0, live, ui);
    }
    ctx.restore();
}

// ---- Clicks ----

void GameUI::botsClick(Game& game, int action, int arg) {
    const bots::Lists& lists = game.botsLists;
    BotsSlot* slot = arg >= 0 && arg < 4 ? &botsSetup.slots[arg] : nullptr;
    switch (action) {
        case BOTS_FORMAT:
            botsSetup.format = std::min(3, std::max(0, arg));
            break;
        case BOTS_WHO_PREV:
        case BOTS_WHO_NEXT: {
            if (!slot) return;
            int options = (int)lists.bots.size() + 1;
            slot->who = (slot->who + (action == BOTS_WHO_NEXT ? 1 : options - 1)) % options;
            slot->bot = slot->who > 0 ? lists.bots[(size_t)slot->who - 1].name : "";
            fitBotsSetup(game);
            break;
        }
        case BOTS_TANK_PREV:
        case BOTS_TANK_NEXT: {
            if (!slot) return;
            const std::vector<int>& tanks = tanksFor(game, slot->who);
            if (tanks.empty()) return;
            int n = (int)tanks.size();
            int at = (int)(std::find(tanks.begin(), tanks.end(), slot->tank) - tanks.begin()) % n;
            slot->tank = tanks[(size_t)((at + (action == BOTS_TANK_NEXT ? 1 : n - 1)) % n)];
            break;
        }
        case BOTS_PICKER_OPEN:
            if (!slot) return;
            botsPickerSlot = botsPickerShown = arg;
            botsPickerScroll = 0;
            botsPickerIcons.clear();
            return;
        case BOTS_PICK:
            botsSetup.slots[botsPickerShown].tank = arg;
            botsPickerSlot = -1;
            break;
        case BOTS_PICKER_CLOSE:
            botsPickerSlot = -1;
            return;
        case BOTS_BEST:
            botsSetup.best = !botsSetup.best;
            break;
        case BOTS_START:
            botsStart(game);
            return;
        case BOTS_READY: {
            int tank = myTank(game.botsRoom);
            game.sendBotsReady({takeName(), tank >= 0 ? botsOwnBuild(game, tank) : std::nullopt});
            return;
        }
        case BOTS_BUILD_OPEN:
            botsBuildTank = botsBuildShown = arg;
            botsBuildScreen = botsScreen;
            return;
        case BOTS_BUILD_CLOSE:
            closeBotsBuild(game);
            return;
        case BOTS_BUILD_SET:
        case BOTS_BUILD_LESS:
        case BOTS_BUILD_MORE: {
            int tank = botsBuildTank, stat = action == BOTS_BUILD_SET ? arg / 16 : arg;
            if (tank < 0 || stat < 0 || stat > 7) return;
            bots::Build build = botsBuild(game, tank);
            int want = action == BOTS_BUILD_SET ? arg % 16 : build[stat] + (action == BOTS_BUILD_MORE ? 1 : -1);
            // A click on the last filled segment takes it back.
            if (action == BOTS_BUILD_SET && want == build[stat]) --want;
            int spent = 0;
            for (int x : build) spent += x;
            int most = std::min(tankdefs::statMax(tank, stat), bots::BUILD_POINTS - (spent - build[stat]));
            build[stat] = std::max(0, std::min(want, most));
            setBotsBuild(game, tank, build);
            return;
        }
        case BOTS_BUILD_RESET:
            if (botsBuildTank < 0) return;
            botsBuilds.erase(botsBuildTank);
            saveBotsBuilds();
            return;
        case BOTS_LEAVE:
            game.sendBotsLeave();
            return;
        case BOTS_REMATCH:
            botsVoted = true;
            game.sendBotsRematch();
            return;
        default:
            return;
    }
    saveBotsSetup();
}
