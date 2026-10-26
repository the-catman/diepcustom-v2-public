#include "GameUI.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <emscripten/emscripten.h>

#include "Achievements.h"
#include "Const.h"
#include "Game.h"
#include "GameRendering.h"
#include "TankDefs.h"

namespace {
    constexpr int ACTION_STAT = 1;
    constexpr int ACTION_UPGRADE = 2;
    constexpr int ACTION_IGNORE = 3;
    constexpr int ACTION_GAMEMODE = 4;
    constexpr int ACTION_PARTY = 5;

    float approach(float value, float target, float dtMs, float rate) {
        float k = 1.0f - std::pow(1.0f - rate, dtMs / 16.6667f);
        return value + (target - value) * k;
    }

    double clamp01(double t) { return t < 0 ? 0 : t > 1 ? 1 : t; }

    // Truncated to an int. The fades settle just under 1, so a resting panel sits at
    // int(H/2) - 1.
    double slideAnchor(float fade, int h, double ui) {
        float u200 = (float)(ui * 200.0);
        return (double)(int)(fade * ((float)(h / 2) + u200) - u200);
    }

    uint32_t blendChannel3(uint32_t rgb, float mul, float add) {
        auto c = [&](int shift) {
            float v = (float)((rgb >> shift) & 0xff) * mul + add;
            return (uint32_t)std::min(255.0f, std::max(0.0f, v)) << shift;
        };
        return c(16) | c(8) | c(0);
    }
    uint32_t pastel(uint32_t rgb) { return blendChannel3(rgb, 0.6f, 102.0f); }

    std::string formatCommas(long long v) {
        std::string s = std::to_string(v);
        for (int i = (int)s.size() - 3; i > (v < 0 ? 1 : 0); i -= 3) s.insert((size_t)i, ",");
        return s;
    }

    std::string formatTimeAlive(double seconds) {
        long long total = (long long)seconds;
        long long hours = total / 3600, minutes = total / 60 % 60, secs = total % 60;
        std::string out;
        if (hours) out += std::to_string(hours) + "h ";
        if (hours || minutes) out += std::to_string(minutes) + "m ";
        out += std::to_string(secs) + 's';
        return out;
    }

    std::string trim(const std::string& s) {
        size_t a = s.find_first_not_of(" \t\n\r");
        if (a == std::string::npos) return "";
        return s.substr(a, s.find_last_not_of(" \t\n\r") - a + 1);
    }
}

EM_JS(void, ui_load_name, (char* out, int max), {
    const name = window.localStorage.getItem("name") || "";
    stringToUTF8(name, out, max);
});

EM_JS(void, ui_save_name, (const char* name), {
    window.localStorage.setItem("name", UTF8ToString(name));
});

// util_set_changelog splits on a literal backslash-n.
EM_JS(void, ui_changelog_set, (const char* text), {
    window.changelog = UTF8ToString(text).split(String.fromCharCode(92) + "n");
});

EM_JS(void, ui_set_dev_password, (const char* password), {
    window.localStorage.setItem("password", UTF8ToString(password));
});

EM_JS(void, ui_changelog_read, (char* out, int max), {
    const lines = Array.isArray(window.changelog) ? window.changelog : [];
    stringToUTF8(lines.map(String).join("\n"), out, max);
});

// Tries the async clipboard API first.
EM_JS(void, ui_copy_text, (const char* text), {
    const value = UTF8ToString(text);
    const fallback = () => {
        const el = document.createElement("textarea");
        el.value = value;
        el.style.position = "fixed";
        el.style.opacity = "0";
        document.body.appendChild(el);
        el.select();
        try { document.execCommand("copy"); } catch (e) {}
        document.body.removeChild(el);
    };
    if (navigator.clipboard && window.isSecureContext) navigator.clipboard.writeText(value).catch(fallback);
    else fallback();
});

EM_JS(void, ui_hide_loading, (), {
    const el = document.getElementById("loading");
    if (el && el.parentNode) el.parentNode.removeChild(el);
});

// The canvas draws the box; this DOM input sits on top and owns caret/selection/clipboard.
EM_JS(void, ui_textfield_open, (const char* value, int maxLen), {
    let el = document.getElementById("ui-textfield");
    if (!el) {
        el = document.createElement("input");
        el.id = "ui-textfield";
        el.type = "text";
        el.autocomplete = "off";
        el.autocapitalize = "off";
        el.spellcheck = false;
        el.style.position = "absolute";
        el.style.margin = "0";
        el.style.padding = "0";
        el.style.border = "0";
        el.style.outline = "0";
        el.style.background = "transparent";
        el.style.boxSizing = "border-box";
        el.style.zIndex = "10";
        el.style.fontFamily = "Ubuntu, sans-serif";
        document.body.appendChild(el);
    }
    el.maxLength = maxLen;
    el.value = UTF8ToString(value);
    el.style.display = "block";
    el.focus();
    const n = el.value.length;
    el.setSelectionRange(n, n);
});

EM_JS(void, ui_textfield_place, (double x, double y, double w, double h, double fontPx, int color, int centered, int outline), {
    const el = document.getElementById("ui-textfield");
    if (!el) return;
    el.style.left = x + "px";
    el.style.top = y + "px";
    el.style.width = w + "px";
    el.style.height = h + "px";
    el.style.fontSize = fontPx + "px";
    el.style.lineHeight = h + "px";
    el.style.textAlign = centered ? "center" : "left";
    el.style.paddingLeft = centered ? "0" : (h * 0.08) + "px";
    el.style.paddingRight = centered ? "0" : (h * 0.08) + "px";
    el.style.color = "#" + ((color >>> 0) & 0xffffff).toString(16).padStart(6, "0");
    if (outline) {
        const s = Math.max(1, h * 0.06);
        el.style.textShadow = (-s) + "px 0 #000," + s + "px 0 #000,0 " + (-s) + "px #000,0 " + s + "px #000";
    } else {
        el.style.textShadow = "none";
    }
    // Keep focus while the box is up; clicking the canvas would otherwise blur it.
    if (document.activeElement !== el) el.focus();
});

EM_JS(void, ui_textfield_read, (char* out, int max), {
    const el = document.getElementById("ui-textfield");
    stringToUTF8(el ? el.value : "", out, max);
});

EM_JS(void, ui_textfield_set, (const char* value), {
    const el = document.getElementById("ui-textfield");
    if (!el) return;
    el.value = UTF8ToString(value);
    const n = el.value.length;
    el.setSelectionRange(n, n);
});

EM_JS(void, ui_textfield_hide, (), {
    const el = document.getElementById("ui-textfield");
    if (!el) return;
    el.style.display = "none";
    el.blur();
});

static void ui_textfield_read_into(std::string& out) {
    char buf[512];
    ui_textfield_read(buf, sizeof(buf));
    out = buf;
}

void GameUI::render(cp5::Context& ctx, Game& game, double now) {
    hitBoxes.clear();
    if (!loadingHidden) { loadingHidden = true; ui_hide_loading(); }
    // ren_ui off skips the whole UI pass, console included.
    if (!config::renUi) {
        if (textFieldActive) { textFieldActive = 0; ui_textfield_hide(); }
        updateHover(0, 0);
        return;
    }

    Simulation& sim = game.simulation;
    double w = cp5::canvasWidth(), h = cp5::canvasHeight();
    double ui = std::max(w / 1920.0, h / 1080.0) * config::uiScale;
    float dt = game.rendering.dt();
    double dpr = cp5::pixelRatio();
    double mx = game.mouseScreenX * dpr, my = game.mouseScreenY * dpr;

    Entity* camera = sim.getCamera();
    CameraData* cam = camera && camera->camera ? &*camera->camera : nullptr;
    bool hasPlayer = cam && sim.get(cam->player);

    bool waitingStart = cam && (cam->flags & CameraFlags::gameWaitingStart);
    Entity* arenaEnt = sim.getArena();
    bool noJoining = arenaEnt && arenaEnt->arena && (arenaEnt->arena->flags & ArenaFlags::noJoining);
    // With a camera and a live connection but no player, the death screen shows while
    // Game::deathPending holds, else the spawn screen (unless joining is closed or the game is
    // waiting to start).
    bool death = game.accepted && cam && !hasPlayer && game.deathPending;
    bool spawn = game.accepted && cam && !hasPlayer && !game.deathPending && !noJoining && !waitingStart;
    awaitingContinue = death;
    // Neither comes up until 500ms after the last frame with neither (200ms after continuing
    // from the death screen).
    if (death || spawn) {
        if (now < screensAfter) death = spawn = false;
    } else {
        screensAfter = now + 500;
    }
    spawnTarget = spawn;
    spawnAlpha = approach(spawnAlpha, spawnTarget ? 1.0f : 0.0f, dt, spawnTarget ? 0.05f : 0.1f);
    deathTarget = death;
    deathAlpha = approach(deathAlpha, deathTarget ? 1.0f : 0.0f, dt, deathTarget ? 0.05f : 0.1f);
    // An Enter press (outside the console) or the pad's Start held confirms both screens.
    // Continuing sends ToRespawn once. The spawn screen keeps sending Spawn every confirming
    // frame until the player shows up.
    bool confirm = enterPressed || game.padHeld[9];
    enterPressed = false;
    if (deathAlpha > 0.2 && game.deathPending && (confirm || !cam)) {
        screensAfter = now + 200;
        game.deathPending = false;
        game.sendRespawn();
    }
    if (spawnTarget && spawnAlpha > 0.2 && confirm) submitSpawn(game);

    if (!hudLayer) hudLayer = std::make_unique<cp5::Context>(true);
    hudLayer->beginLayer((int)w, (int)h);

    // The latency line only shows while L is held with a player.
    std::string pingText;
    if (game.showPing && hasPlayer && game.pingAverage() >= 0) {
        char buf[96];
        std::snprintf(buf, sizeof(buf), "%.1f ms %s", game.pingAverage(), game.serverHost.c_str());
        pingText = buf;
    }
    // No HUD until the connection's first spawn.
    game.rendering.renderHUD(*hudLayer, sim, now, mx, my, game.playersOnline, pingText, game.spawnedThisConnection);
    if (game.spawnedThisConnection) {
        if (config::renStats) renderStats(*hudLayer, game, ui, mx, my);
        renderNotifications(*hudLayer, game, now, ui);
        renderClassTree(*hudLayer, game, now, hasPlayer);
    }
    if (hasPlayer && config::renAchievements) renderAchievements(*hudLayer, game, now, ui, hasPlayer, mx);

    ctx.save();
    ctx.resetTransform();
    ctx.setAlpha(0.7);
    hudLayer->compositeLayer(ctx);
    ctx.setAlpha(1);

    if (config::renUpgrades) renderUpgrades(ctx, game, now, ui, mx, my);

    // Dims whenever there is no player, connected or not.
    float dimTarget = hasPlayer ? 0.0f : 1.0f;
    dimAlpha = approach(dimAlpha, dimTarget, dt, dimTarget > dimAlpha ? 0.05f : 0.1f);
    if (dimAlpha > 0.01) {
        ctx.setAlpha(dimAlpha * 0.4);
        ctx.setFillColor(0x000000);
        ctx.fillRect(0, 0, w, h);
        ctx.setAlpha(1);
        // Covers the HUD buttons and blocks fire.
        hitBoxes.push_back({0, 0, w, h, 0, 0});
    }

    renderChangelog(ctx, game, ui, hasPlayer);
    renderConnecting(ctx, game, ui);
    renderCountdown(ctx, game, now, ui, game.accepted && cam && !hasPlayer && !game.deathPending && !noJoining && waitingStart);
    renderGamemodeStack(ctx, game, ui);
    renderPartyButton(ctx, game, ui, hasPlayer, mx, my);
    if (!hasPlayer && config::renAchievements) renderAchievements(ctx, game, now, ui, hasPlayer, mx);
    if (deathAlpha > 0.01) renderDeathScreen(ctx, game, now, ui);
    if (spawnAlpha > 0.01) renderSpawnScreen(ctx, game, now, ui);
    renderFps(ctx, game, ui);
    if (consoleOpen) renderConsole(ctx, game, ui, now);

    ctx.restore();
    padInput(game, hasPlayer);
    updateHover(mx, my);
    syncTextInput();
    // Wheel input only lives for one frame.
    wheelAccum = 0;
}

void GameUI::syncTextInput() {
    int field = consoleOpen ? 1 : (spawnAlpha > 0.7 ? 2 : 0);
    if (field != textFieldActive) {
        textFieldActive = field;
        if (!field) ui_textfield_hide();
        else ui_textfield_open((field == 1 ? consoleInput : nameInput).c_str(), field == 1 ? 128 : 16);
    }
    if (!field) return;
    double dpr = cp5::pixelRatio();
    ui_textfield_place(tfX / dpr, tfY / dpr, tfW / dpr, tfH / dpr, tfFont / dpr, tfColor, tfCentered, tfOutline);
    char buf[512];
    ui_textfield_read(buf, sizeof(buf));
    (field == 1 ? consoleInput : nameInput) = buf;
    if (field == 2 && nameInput != nameFieldSaved) {
        nameFieldSaved = nameInput;
        ui_save_name(trim(nameInput).c_str());
    }
}

// ---- Spawn / death panels ----

void GameUI::renderSpawnScreen(cp5::Context& ctx, Game& game, double now, double ui) {
    if (!nameLoaded) {
        nameLoaded = true;
        char buf[64] = {0};
        ui_load_name(buf, sizeof(buf));
        nameInput = buf;
    }

    double cx = cp5::canvasWidth() / 2;
    double cy = slideAnchor(spawnAlpha, cp5::canvasHeight(), ui);

    ctx.save();
    ctx.setAlpha(spawnAlpha);

    game.rendering.drawLabel(ctx, "This is the tale of...", cx, cy - 45 * ui, 24 * ui, 0xffffff);

    // Whole pixels: truncated size, centered by int halves.
    int boxW = (int)(float)(400 * ui), boxH = (int)(float)(50 * ui);
    double boxX = cx - boxW / 2, boxY = cy - boxH / 2;
    ctx.setFillColor(0xeeeeee);
    ctx.fillRect(boxX, boxY, boxW, boxH);
    ctx.setStrokeColor(0x000000);
    ctx.setLineWidth(5 * ui);
    ctx.setLineJoin(cp5::LineJoin::miter);
    ctx.strokeRect(boxX, boxY, boxW, boxH);

    hitBoxes.push_back({boxX, boxY, (double)boxW, (double)boxH, 0, 0, CURSOR_TEXT});
    bool nameActive = spawnAlpha > 0.7 && !consoleOpen;
    if (nameActive) {
        tfX = boxX; tfY = boxY; tfW = boxW; tfH = boxH;
        tfFont = boxH * 0.9; tfColor = 0x000000; tfCentered = false; tfOutline = false;
    } else if (!nameInput.empty()) {
        ctx.setFontSize(boxH * 0.9);
        ctx.setTextBaseline(cp5::Baseline::middle);
        ctx.setFillColor(0x000000);
        double tw = ctx.measureText(nameInput);
        double tx = boxX + 4 * ui;
        if (tw > boxW - 8 * ui) tx -= tw - (boxW - 8 * ui);
        ctx.fillText(nameInput, tx, cy);
    }
    (void)now;

    game.rendering.drawLabel(ctx, "(press enter to spawn)", cx, cy + 39 * ui, 14 * ui, 0xffffff);
    Entity* camera = game.simulation.getCamera();
    if (camera && camera->camera && camera->camera->respawnLevel >= 2) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "You will spawn at level %d", (int)camera->camera->respawnLevel);
        game.rendering.drawLabel(ctx, buf, cx, cy + 64 * ui, 14 * ui, 0xffffff);
    }
    ctx.restore();
}

// Top-center gamemode buttons, shown whenever there is no player. Notifications hang
// below them.
void GameUI::renderGamemodeStack(cp5::Context& ctx, Game& game, double ui) {
    Simulation& sim = game.simulation;
    Entity* camera = sim.getCamera();
    CameraData* cam = camera && camera->camera ? &*camera->camera : nullptr;
    bool hasPlayer = cam && sim.get(cam->player);
    bool show = !hasPlayer;
    stackAlpha = approach(stackAlpha, show ? 1.0f : 0.0f, game.rendering.dt(), show ? 0.05f : 0.1f);

    int servers = tankdefs::serverCount();
    bool joining = game.joiningParty;
    if (stackAlpha < 0.01 || (servers <= 0 && !joining)) { stackBottomU = 24; return; }

    double w = cp5::canvasWidth();
    int cx = cp5::canvasWidth() / 2;
    double dpr = cp5::pixelRatio();
    double mx = game.mouseScreenX * dpr, my = game.mouseScreenY * dpr;
    auto unit = GameRendering::uiUnits;
    float bw = unit(85), bh = unit(24), gap = unit(10);
    // While a party link's server lookup runs, "Joining party..." replaces the title and
    // there are no buttons.
    double titleSize = (double)unit(20);
    const char* title = joining ? "Joining party..." : "Game mode";
    GameRendering::TextBox titleBox = game.rendering.textBox(ctx, title, titleSize);
    float firstRow = unit(24) + (float)titleBox.h + unit(5);

    int rows = joining ? 0 : (servers + 3) / 4;
    float rowY = firstRow;
    for (int r = 0; r < rows; ++r) rowY = rowY + bh + gap;
    double bottom = joining ? (double)(unit(24) + (float)titleBox.h) : rowY - 4 * ui;
    double layerH = std::ceil(joining ? bottom : bottom + bh);
    if (!stackLayer) stackLayer = std::make_unique<cp5::Context>(true);
    stackLayer->beginLayer((int)w, (int)layerH);
    game.rendering.drawTextBox(*stackLayer, title, titleBox, std::round((float)cx - (float)(titleBox.w / 2)),
                               std::round(unit(24) - (float)(titleBox.h / 2)), titleSize, 0xffffff);

    float y = firstRow;
    for (int i = 0; i < servers && !joining; ++i) {
        std::string mode = tankdefs::serverGamemode(i);
        int col = i % 4, row = i / 4;
        int rowCount = std::min(4, servers - row * 4);
        double shift = row < rows - 1 || servers % 4 == 0 ? 1.5 : (double)(rowCount - 1) * 0.5;
        float bx = (float)(((double)col - shift) * (double)(bw + gap) + (double)cx) - bw * 0.5f;
        float by = y - bh * 0.5f;
        if (col == 3) y = y + bh + gap;
        bool selected = mode == game.gamemode;
        bool over = mx >= bx && mx <= bx + bw && my >= by && my <= by + bh;
        bool pressed = selected || (over && pressedButton(ACTION_GAMEMODE, i));
        drawButton(*stackLayer, bx, by, bw, bh, teamColorHex(i & 7), !selected, over, pressed, tankdefs::serverName(i), ui,
                   0x333333, 7.5);
        addButton(bx, by, bw, bh, ACTION_GAMEMODE, i, stackAlpha > 0.9 && !selected);
    }
    ctx.save();
    ctx.setAlpha(stackAlpha);
    stackLayer->compositeLayer(ctx);
    ctx.restore();
    stackBottomU = bottom / ui;
}

// "Copy party link": top right without a player, once the server sent our party code.
void GameUI::renderPartyButton(cp5::Context& ctx, Game& game, double ui, bool hasPlayer, double mx, double my) {
    bool show = game.accepted && !game.gamemode.empty() && !hasPlayer && game.partyReceived;
    partyAlpha = approach(partyAlpha, show ? 1.0f : 0.0f, game.rendering.dt(), show ? 0.05f : 0.1f);
    if (partyAlpha < 0.01) return;

    double bw = 120 * ui, bh = 24 * ui;
    double bx = cp5::canvasWidth() - bw - 15 * ui, by = 15 * ui;
    bool clickable = show && partyAlpha > 0.9;
    bool over = mx >= bx && mx <= bx + bw && my >= by && my <= by + bh;
    ctx.save();
    ctx.scaleAlpha(partyAlpha);
    drawButton(ctx, bx, by, bw, bh, teamColorHex(-1), clickable, over, over && pressedButton(ACTION_PARTY, 0),
               "Copy party link", ui, 0x333333);
    ctx.restore();
    addButton(bx, by, bw, bh, ACTION_PARTY, 0, clickable);
}

void GameUI::renderDeathScreen(cp5::Context& ctx, Game& game, double now, double ui) {
    Entity* camera = game.simulation.getCamera();
    if (!camera || !camera->camera) return;
    CameraData& cam = *camera->camera;

    double cx = cp5::canvasWidth() / 2;
    double cy = slideAnchor(deathAlpha, cp5::canvasHeight(), ui);

    ctx.save();
    ctx.setAlpha(deathAlpha);
    ctx.translate(cx, cy);

    // With no killer name the mystery line goes under it and the whole block rises by that
    // line's height.
    GameRendering& r = game.rendering;
    auto centered = [&](const std::string& text, const GameRendering::TextBox& box, float y, double size) {
        r.drawTextBox(ctx, text, box, -(box.w / 2), std::round(y - (float)(box.h / 2)), size, 0xffffff);
    };
    bool unnamed = cam.killedBy.empty();
    const std::string mystery = "(they seem to prefer to keep an air of mystery about them)";
    std::string killer = unnamed ? "An unnamed tank" : cam.killedBy;
    GameRendering::TextBox byBox = r.textBox(ctx, "You were killed by:", 18 * ui);
    GameRendering::TextBox killerBox = r.textBox(ctx, killer, 24 * ui);
    GameRendering::TextBox mysteryBox = r.textBox(ctx, mystery, 14 * ui);
    float top = (float)(-150 * ui) - (unnamed ? mysteryBox.h : 0);
    centered("You were killed by:", byBox, top, 18 * ui);
    centered(killer, killerBox, top + byBox.h, 24 * ui);
    if (unnamed) centered(mystery, mysteryBox, top + byBox.h + killerBox.h + (float)(-5 * ui), 14 * ui);

    game.rendering.drawTankPreview(ctx, cam.tank, 100 * ui, -20 * ui, -M_PI / 4, 0.7 * ui);
    std::string tank = !cam.tankOverride.empty() ? cam.tankOverride : tankdefs::name(cam.tank);
    GameRendering::TextBox tankBox = r.textBox(ctx, tank, 18 * ui);
    ctx.save();
    ctx.translate((float)(100 * ui), -(float)(20 * ui));
    centered(tank, tankBox, (float)(75 * ui), 18 * ui);
    ctx.restore();

    int rowX = (int)(float)(-100 * ui);
    int rowY = (int)(float)(-50 * ui);
    auto row = [&](const std::string& label, const std::string& value) {
        GameRendering::TextBox labelBox = r.textBox(ctx, label, 18 * ui);
        GameRendering::TextBox valueBox = r.textBox(ctx, value, 24 * ui);
        r.drawTextBox(ctx, label, labelBox, rowX - labelBox.w, rowY - labelBox.h / 2, 18 * ui, 0xffffff);
        r.drawTextBox(ctx, value, valueBox, rowX, rowY - valueBox.h / 2, 24 * ui, 0xffffff);
        rowY += valueBox.h;
    };
    row("Score:", formatCommas((long long)cam.score.target));
    row("Level:", std::to_string((int)cam.level));
    if (cam.deathTick > cam.spawnTick) {
        row("Time Alive:", formatTimeAlive((cam.deathTick - cam.spawnTick) / 25.0));
    }

    GameRendering::TextBox hintBox = r.textBox(ctx, "(press enter to continue)", 18 * ui);
    centered("(press enter to continue)", hintBox, (float)(100 * ui), 18 * ui);
    (void)now;
    ctx.restore();
}

// "Waiting for players..." until the arena is ready, then "Game starting in N" while the
// countdown is above 0. The starting text keeps its last value while it fades out.
void GameUI::renderCountdown(cp5::Context& ctx, Game& game, double now, double ui, bool lobby) {
    Entity* arena = game.simulation.getArena();
    ArenaData* a = arena && arena->arena ? &*arena->arena : nullptr;
    bool ready = a && (a->flags & ArenaFlags::gameReadyStart);
    bool waiting = lobby && !ready;
    bool starting = lobby && ready && a->ticksUntilStart.getSmooth(now) / 25.0f > 0;
    float dt = game.rendering.dt();
    waitAlpha = approach(waitAlpha, waiting ? 1.0f : 0.0f, dt, waiting ? 0.05f : 0.1f);
    startAlpha = approach(startAlpha, starting ? 1.0f : 0.0f, dt, starting ? 0.05f : 0.1f);
    double cx = cp5::canvasWidth() / 2;
    int h = cp5::canvasHeight();

    if (waitAlpha >= 0.01) {
        double cy = slideAnchor(waitAlpha, h, ui);
        ctx.save();
        ctx.setAlpha(waitAlpha);
        game.rendering.drawLabel(ctx, "Waiting for players...", cx, cy, 70 * ui, 0xffffff, true, 0x000000, 0.1);
        if (a) {
            int needed = (int)a->playersNeeded;
            waitSub = std::to_string(needed) + " more player" + (needed == 1 ? "" : "s") + " needed";
        }
        game.rendering.drawLabel(ctx, waitSub, cx, cy + 63 * ui, 25 * ui, 0xffffff);
        ctx.restore();
    }
    if (startAlpha >= 0.01) {
        double cy = slideAnchor(startAlpha, h, ui);
        if (starting) startText = "Game starting in " + std::to_string((int)std::ceil(a->ticksUntilStart.getSmooth(now) / 25.0f));
        ctx.save();
        ctx.setAlpha(startAlpha);
        game.rendering.drawLabel(ctx, startText, cx, cy, 70 * ui, 0xffffff, true, 0x000000, 0.1);
        ctx.restore();
    }
}

// Changelog: top left until the first spawn. Once it fades out under a live player it
// never comes back.
void GameUI::renderChangelog(cp5::Context& ctx, Game& game, double ui, bool hasPlayer) {
    if (!config::renChangelog || changelogDone) return;
    changelogAlpha = approach(changelogAlpha, hasPlayer ? 0.0f : 1.0f, game.rendering.dt(), hasPlayer ? 0.1f : 0.05f);
    if (changelogAlpha < 0.01 && hasPlayer) { changelogDone = true; return; }
    if (!changelogLoaded) {
        changelogLoaded = true;
        changelog.clear();
        std::vector<char> buf(8192);
        ui_changelog_read(buf.data(), (int)buf.size());
        std::string all = buf.data();
        size_t start = 0;
        while (start <= all.size() && !all.empty()) {
            size_t end = all.find('\n', start);
            if (end == std::string::npos) end = all.size();
            changelog.push_back(all.substr(start, end - start));
            start = end + 1;
        }
    }

    // Empty lines still advance.
    int m = (int)GameRendering::uiUnits(30);
    ctx.save();
    ctx.setAlpha(changelogAlpha);
    int y = m;
    auto line = [&](const std::string& text, double size) {
        GameRendering::TextBox box = game.rendering.textBox(ctx, text, size);
        if (!text.empty()) game.rendering.drawTextBox(ctx, text, box, m, y, size, 0xffffff);
        y += box.h;
    };
    line("Changelog", GameRendering::uiUnits(20));
    for (const std::string& text : changelog) line(text, GameRendering::uiUnits(16));

    // The store badges, social icons and "More games" row are left out.
    ctx.restore();
}

// Achievements: a carousel of cards on the right edge. Without a player it shows while the
// mouse is in the right 15% of the screen and drifts upward, wrapping. With one, only cards
// unlocked in the last 15s slide in (drawn into the HUD canvas, so at 0.7).
void GameUI::renderAchievements(cp5::Context& ctx, Game& game, double now, double ui, bool hasPlayer, double mx) {
    int count = achievements::count();
    if (achievementCards.empty()) {
        achievementCards.resize((size_t)count);
        for (int i = 0; i < count; ++i) {
            AchievementCard& c = achievementCards[(size_t)i];
            c.unlocked = achievements::unlocked(i);
            c.hasProgress = achievements::hasProgress(i);
            c.progress = achievements::progress(i);
        }
    } else {
        for (int i = 0; i < count; ++i) {
            AchievementCard& c = achievementCards[(size_t)i];
            bool unlocked = achievements::unlocked(i);
            double progress = achievements::progress(i);
            if (!c.unlocked && unlocked) c.unlockedAt = now;
            if (std::abs(c.progress - progress) > 0.00001) c.canvasColor = -2;
            c.unlocked = unlocked;
            c.progress = progress;
        }
    }

    double w = cp5::canvasWidth(), h = cp5::canvasHeight();
    float dt = game.rendering.dt();
    double k = clamp01(1.0 - std::pow(0.9, dt / 16.666667));
    bool shown = hasPlayer || mx > w * 0.85;

    double overall = 0;
    for (AchievementCard& c : achievementCards) overall = std::max(overall, c.fade);

    achievementScrollVel = (float)((1.0 - k) * achievementScrollVel);
    if (!hasPlayer) {
        achievementsBrowsing = true;
        double scroll = achievementScroll + dt / -50.0;
        if (!consoleOpen) {
            achievementScrollVel += (float)(wheelAccum * 10);
            wheelAccum = 0;
        }
        achievementScroll = scroll - achievementScrollVel;
    } else if (overall < 0.0001) {
        achievementScroll = 0;
        achievementsBrowsing = false;
    }

    auto recent = [&](const AchievementCard& c) { return c.unlockedAt != 0 && now - c.unlockedAt < 15000; };

    int unlockedCount = 0;
    double totalH = 0, listedH = 0;
    for (AchievementCard& c : achievementCards) {
        bool target = shown && (!hasPlayer || recent(c));
        c.fade = k * target + c.fade * (1 - k);
        if (!target && c.fade < 0.001) {
            c.placed = false;
            c.fade = 0;
        }
        unlockedCount += c.unlocked;
        totalH += 60;
        if (achievementsBrowsing || recent(c)) listedH += 60;
    }
    achievementListH = listedH * k + achievementListH * (1 - k);

    double offset = 0, topY = h;
    for (int i = 0; i < count; ++i) {
        AchievementCard& c = achievementCards[(size_t)i];
        bool listed = achievementsBrowsing || recent(c);
        if (!c.placed) {
            c.placed = true;
            c.row = offset;
        } else if (shown && listed) {
            c.row = offset * k + c.row * (1 - k);
        }
        double slot = std::fmod(totalH + std::fmod(c.row + achievementScroll, totalH), totalH);
        double span = achievementsBrowsing ? totalH : achievementListH;
        double rowY = h * 0.5 + (float)(ui * span) * -0.5 + (float)(ui * slot);
        // Cards fade and tuck in toward the screen edge the further they are from the middle.
        double ra = std::pow(clamp01(std::abs(rowY - (double)((int)h / 2)) / (h * -0.4) + 1.0), 0.8);
        double alpha = c.fade * (achievementsBrowsing ? ra : 1.0);
        if (alpha > 0) {
            drawAchievementCard(c, i, game, ui);
            ctx.save();
            ctx.scaleAlpha(alpha);
            double x = w + 50 * ui - c.fade * (std::sqrt(ra) * 20 * ui + c.cardW + 50 * ui + 10 * ui) - 35 * ui;
            ctx.drawContext(*c.canvas, x, rowY - 10 * ui - 25 * ui);
            ctx.restore();
        }
        topY = std::min(topY, rowY);
        if (listed) offset += 60;
    }

    // The header slides in with the brightest card, above the topmost one but never lower
    // than 13.5% from the top.
    double headY = std::max(h * 0.1 * 1.35, topY);
    ctx.save();
    ctx.scaleAlpha(overall);
    double tw, th;
    ctx.setFontSize(28 * ui);
    ctx.setLineWidth(28 * ui * 0.2);
    ctx.labelSize("Achievements", &tw, &th);
    double left = w + 20 * ui - overall * ((int)tw + 15 * ui + 20 * ui);
    double tx = std::round(left);
    double ty = std::round(headY - (int)th - 5 * ui - (int)th / 2);
    game.rendering.drawLabel(ctx, "Achievements", tx + tw / 2, ty + (int)th / 2.0, 28 * ui, 0xffffff);
    if (achievementsBrowsing) {
        std::string sub = std::to_string(unlockedCount) + " out of " + std::to_string(count) + " unlocked";
        double sw, sh;
        ctx.setFontSize(14 * ui);
        ctx.setLineWidth(14 * ui * 0.2);
        ctx.labelSize(sub, &sw, &sh);
        double sx = std::round(left + (int)tw / 2 - (int)sw / 2);
        double sy = std::round(headY - 20 * ui - (int)sh / 2);
        game.rendering.drawLabel(ctx, sub, sx + sw / 2, sy + (int)sh / 2.0, 14 * ui, 0xffffff);
    }
    ctx.restore();
}

// Redrawn when the unit, the color or the progress changes.
void GameUI::drawAchievementCard(AchievementCard& card, int index, Game& game, double ui) {
    int color = card.unlocked ? index : -1;
    if (card.canvas && card.canvasColor == color && std::abs(card.canvasUnit - ui) <= 0.00001) return;
    if (!card.canvas) card.canvas = std::make_unique<cp5::Context>(true);
    card.canvasColor = color;
    card.canvasUnit = ui;
    cp5::Context& cv = *card.canvas;

    std::string name = achievements::name(index), desc = achievements::desc(index);
    bool inProgress = !card.unlocked && card.hasProgress;
    double nw, nh, dw, dh;
    cv.setFontSize(20 * ui);
    cv.setLineWidth(20 * ui * 0.2);
    cv.labelSize(name, &nw, &nh);
    cv.setFontSize(14 * ui);
    cv.setLineWidth(14 * ui * 0.2);
    cv.labelSize(desc, &dw, &dh);
    card.cardW = std::max((double)(int)dw + (inProgress ? 30 * ui : 0), (double)(int)nw) + 10 * ui;

    cv.setSize(std::max(1, (int)(card.cardW + 25 * ui + 20 * ui)), std::max(1, (int)(20 * ui + 50 * ui)));
    cv.clear();
    cv.resetTransform();
    cv.setAlpha(1);
    cv.translate(35 * ui, 10 * ui);

    double r = 25 * ui;
    cv.beginPath();
    cv.moveTo(0, 0);
    cv.lineTo(card.cardW, 0);
    cv.lineTo(card.cardW, 50 * ui);
    cv.lineTo(0, 50 * ui);
    cv.arc(0, r, r, 1.570796, 4.712389);
    cv.closePath();
    cv.setFillColor(blendChannel3(teamColorHex(color), 0.8f, 51.0f));
    cv.fill();
    cv.setStrokeColor(0x333333);
    cv.setLineWidth(5 * ui);
    cv.setLineJoin(cp5::LineJoin::round);
    cv.stroke();

    // Text rasters are placed by their top-left corner.
    auto label = [&](const std::string& text, double size, double x, double centerY, double tw, double th) {
        double y = std::round(centerY - (int)th / 2);
        game.rendering.drawLabel(cv, text, std::round(x) + tw / 2, y + (int)th / 2.0, size, 0xffffff);
    };
    label(name, 20 * ui, -10 * ui, r - 10 * ui, nw, nh);
    label(desc, 14 * ui, 3 * ui - 10 * ui, r + 10 * ui, dw, dh);
    if (inProgress) {
        std::string pct = std::to_string((int)(card.progress * 100)) + "%";
        double pw, ph;
        cv.labelSize(pct, &pw, &ph);
        label(pct, 14 * ui, card.cardW - 5 * ui - (int)pw, r + 10 * ui, pw, ph);
    }
}

// ---- Notifications ----

// New connection: drop everything tied to the previous server.
void GameUI::onNewConnection() {
    notifications.clear();
    config::gameStatsBuild.clear();
    upgradeSlots.clear();
    upgradesIgnored = false;
    ignoreUntilLevel = 0;
    hadPlayer = false;
    queuedSinceUDown = false;
    deathAlpha = 0;
    stackBottomU = 24;
}

void GameUI::addNotification(const std::string& text, uint32_t color, double timeMs, const std::string& id, double now) {
    // A repeated id expires the old entry (fades out over 300ms) instead of stacking.
    if (!id.empty()) {
        for (Notification& n : notifications) {
            if (n.id != id) continue;
            if (!(n.expiry < now + 300)) n.expiry = now + 300;
        }
    }
    Notification n;
    n.text = text;
    n.id = id;
    n.color = color;
    n.created = now;
    n.expiry = timeMs > 0 ? now + timeMs : 0;
    notifications.push_back(n);
}

void GameUI::renderNotifications(cp5::Context& ctx, Game& game, double now, double ui) {
    // The text grows with the fade, and the background is its raster box + 10u.
    auto unit = GameRendering::uiUnits;
    GameRendering& r = game.rendering;
    ctx.save();
    ctx.translate((float)(cp5::canvasWidth() / 2), dimAlpha * ((float)(stackBottomU * ui) - unit(24)) + unit(24));
    float y = unit(20) * 0.5f;

    for (size_t i = 0; i < notifications.size();) {
        Notification& n = notifications[i];
        if (n.expiry > 0 && now >= n.expiry) { notifications.erase(notifications.begin() + (long)i); continue; }
        float fade = std::min(1.0f, std::max(0.0f, (float)((now - n.created) / 300.0)));
        if (n.expiry > 0) fade = std::min(fade, std::min(1.0f, std::max(0.0f, (float)((n.expiry - now) / 300.0))));
        fade = fade * fade * (3.0f - (fade + fade));

        double size = (double)(fade * unit(20));
        GameRendering::TextBox box = r.textBox(ctx, n.text, size);
        float bgW = (float)box.w + unit(10);
        ctx.setAlpha((double)fade * 0.5);
        ctx.setFillColor(n.color);
        ctx.fillRect(bgW * -0.5f + 0.0f, y - (float)(box.h / 2), bgW, box.h);
        ctx.setAlpha(fade);
        r.drawTextBox(ctx, n.text, box, std::round(0.0f - (float)(box.w / 2)), std::round(y - (float)(box.h / 2)), size,
                      0xffffff, false);

        y = y + fade * unit(5) + (float)box.h;
        ++i;
    }
    ctx.restore();
}

// ---- Stat upgrades ----

// Predicts locally (level+1, points-1), then sends StatUpgrade with the target level.
void GameUI::applyStatUpgrade(Game& game, int statId, int targetLevel) {
    Entity* camera = game.simulation.getCamera();
    if (statId < 0 || statId >= 8 || !camera || !camera->camera) return;
    CameraData& cam = *camera->camera;
    if (!game.simulation.get(cam.player)) return;
    if ((int)cam.statsAvailable < 1) return;
    int level = (int)cam.statLevels[statId];
    if (level >= (int)cam.statLimits[statId]) return;
    if (targetLevel != -1 && level >= targetLevel) return;
    cam.statLevels[statId] = level + 1;
    cam.statsAvailable -= 1;
    game.sendStatUpgrade(statId, targetLevel);
}

void GameUI::queueStat(Game& game, int statId, bool toMax) {
    Entity* camera = game.simulation.getCamera();
    if (statId < 0 || statId >= 8 || !camera || !camera->camera) return;
    CameraData& cam = *camera->camera;
    int limit = (int)cam.statLimits[statId];
    if (limit <= 0) return;

    int counts[8] = {};
    for (char qc : config::gameStatsBuild) {
        int s = '8' - qc;
        if (s >= 0 && s < 8) ++counts[s];
    }
    // Appends stop once total committed points (max(level, queued) per stat) pass 33.
    int committed = 0;
    for (int s = 0; s < 8; ++s) committed += std::max((int)cam.statLevels[s], counts[s]);

    int level = (int)cam.statLevels[statId];
    int count = counts[statId];
    do {
        if (committed > 32) return;
        if (std::max(count, level) >= limit) return;
        // Entries at or below the current level pad the FIFO without costing points.
        for (; count < level; ++count) config::gameStatsBuild += (char)('8' - statId);
        config::gameStatsBuild += (char)('8' - statId);
        ++count;
        ++committed;
        queuedSinceUDown = true;
    } while (toMax);
}

void GameUI::statAction(Game& game, int statId) {
    Entity* camera = game.simulation.getCamera();
    if (!camera || !camera->camera || !game.simulation.get(camera->camera->player)) return;
    if (game.keyHeld(Key::M)) queueStat(game, statId, true);
    else if (game.keyHeld(Key::U)) queueStat(game, statId, false);
    else applyStatUpgrade(game, statId, -1);
}

void GameUI::renderStats(cp5::Context& ctx, Game& game, double ui, double mx, double my) {
    Entity* camera = game.simulation.getCamera();
    if (!camera || !camera->camera) return;
    CameraData& cam = *camera->camera;
    bool hasPlayer = game.simulation.get(cam.player) != nullptr;
    float dt = game.rendering.dt();
    double h = cp5::canvasHeight();

    if (!hasPlayer && hadPlayer) config::gameStatsBuild.clear();
    hadPlayer = hasPlayer;

    // Tapping U without queueing anything clears the queue.
    bool uHeld = game.keyHeld(Key::U);
    if (uHeld && !lastUHeld) queuedSinceUDown = false;
    if (!uHeld && lastUHeld && !queuedSinceUDown) config::gameStatsBuild.clear();
    lastUHeld = uHeld;

    // Walk the queue FIFO, re-sending entries the server hasn't applied.
    int counts[8] = {};
    int spendable = (int)cam.statsAvailable;
    for (char qc : config::gameStatsBuild) {
        int s = '8' - qc;
        if (s < 0 || s > 7) continue;
        int k = ++counts[s];
        if (!hasPlayer || spendable < 1) continue;
        if ((int)cam.statLevels[s] > k - 1) continue;
        if (k - 1 >= (int)cam.statLimits[s]) continue;
        applyStatUpgrade(game, s, k);
        --spendable;
    }

    bool points = cam.statsAvailable > 0;
    // The panel latches open from the small hover zone, from U/M held, or from points with a
    // player, and stays open while the mouse is in the wider zone. The death screen holds it open
    // until continued; otherwise it closes without a player.
    bool keys = game.keyHeld(Key::U) || game.keyHeld(Key::M);
    float rate = 0.05f, target;
    if (awaitingContinue) {
        statsLatch = true;
        target = 1;
    } else {
        if (!(mx <= 250 * ui && my >= h - 392 * ui)) statsLatch = false;
        if (mx < 80 * ui && my > h - 342 * ui) statsLatch = true;
        if (keys || (points && hasPlayer)) statsLatch = true;
        if (keys) rate = 0.15f;
        if (!hasPlayer) statsLatch = false;
        target = statsLatch ? 1.0f : 0.0f;
    }
    statsOpen = approach(statsOpen, target, dt, rate);
    if (statsOpen < 0.001) return;

    // The "xN" badge uses the unrounded x.
    auto unit = GameRendering::uiUnits;
    float u24 = unit(24), u30 = unit(30), segW = u24 - unit(4), segH = unit(16), gap = unit(4);
    float xf = (float)((double)(unit(10) + u30) + (1.0 - (double)statsOpen) * (double)unit(-400));
    int x = (int)xf;
    float maxLen = 0;
    int rows = 0;

    ctx.save();
    for (int i = 0; i < 8; ++i) {
        int limit = (int)cam.statLimits[i];
        if (limit <= 0) continue;
        int level = std::min((int)cam.statLevels[i], limit);
        int y = (int)((float)h + unit(-10) - u30 - (float)rows * u24);
        float barLen = (float)limit * u24 + u30;
        float barEnd = barLen + (float)x;
        uint32_t color = teamColorHex(i);

        ctx.setLineCap(cp5::LineCap::round);
        ctx.beginPath();
        ctx.moveTo(x, y);
        ctx.lineTo(barEnd, y);
        ctx.setLineWidth(unit(20));
        ctx.setStrokeColor(0x000000);
        ctx.stroke();

        // The first segment is rounded on the left, the rest are plain rects.
        auto segments = [&](int from, int to, uint32_t rgb) {
            ctx.setFillColor(rgb);
            float sx = (segW + gap) * (float)from + (float)x;
            for (int s = from; s < std::min(limit, to); ++s, sx = sx + segW + gap) {
                if (s == 0) {
                    ctx.beginPath();
                    ctx.arc(x, y, segH * 0.5f, M_PI / 2, -M_PI / 2);
                    ctx.lineTo(segW + (float)x, (float)y - segH * 0.5f);
                    ctx.lineTo(segW + (float)x, segH * 0.5f + (float)y);
                    ctx.closePath();
                    ctx.fill();
                } else {
                    ctx.fillRect(sx, (float)y + segH * -0.5f, segW, segH);
                }
            }
        };
        segments(0, level, color);
        if (counts[i] > level) segments(level, counts[i], blendChannel3(color, 0.5f, 0.0f));

        bool canUpgrade = points && level < limit && hasPlayer;
        // The "+" zone moves with the slide.
        float zoneX = barEnd - u30, zoneY = (float)y - segH * 0.5f, zoneW = segH * 0.5f + u30;
        bool over = mx >= zoneX && mx <= zoneX + zoneW && my >= zoneY && my <= zoneY + segH;
        uint32_t bcol = canUpgrade ? color : 0x888888;
        if (canUpgrade && over && pressedButton(ACTION_STAT, i)) bcol = blendChannel3(bcol, 0.8f, 0.0f);
        else if (canUpgrade && over) bcol = blendChannel3(bcol, 0.8f, 51.0f);
        ctx.beginPath();
        ctx.arc(barEnd, y, unit(8), -M_PI / 2, M_PI / 2);
        ctx.lineTo(barEnd - u30, segH * 0.5f + (float)y);
        ctx.lineTo(barEnd - u30, (float)y + segH * -0.5f);
        ctx.lineTo(barEnd, (float)y + segH * -0.5f);
        ctx.setFillColor(bcol);
        ctx.fill();

        int half = (int)((double)segH * 0.3);
        float px = segH * 0.25f + barEnd - u30 * 0.5f;
        ctx.setLineCap(cp5::LineCap::square);
        ctx.setStrokeColor(0x000000);
        ctx.setLineWidth(unit(4));
        ctx.beginPath();
        ctx.moveTo(px, y - half);
        ctx.lineTo(px, y + half);
        ctx.moveTo(px - (float)half, y);
        ctx.lineTo(px + (float)half, y);
        ctx.stroke();

        GameRendering& r = game.rendering;
        double nameSize = (double)unit(13);
        GameRendering::TextBox nameBox = r.textBox(ctx, cam.statNames[i], nameSize);
        r.drawTextBox(ctx, cam.statNames[i], nameBox, std::round((barLen - u30) * 0.5f + (float)x - (float)(nameBox.w / 2)),
                      std::round((float)y - (float)(nameBox.h / 2)), nameSize, 0xffffff);
        // The hint centers 10u left of barEnd - 34u, or with a gamepad ends there.
        char hint[8];
        float hintX = barLen + (float)x - u30 - gap;
        double hintSize = (double)unit(10);
        double hintLeft;
        GameRendering::TextBox hintBox;
        if (game.padActive()) {
            static const char* letters[2] = {"X", "Y"};
            static const char* arrows[4] = {"\u2191", "\u2192", "\u2193", "\u2190"};
            std::snprintf(hint, sizeof(hint), "%s%s", letters[(7 - i) / 4], arrows[(7 - i) % 4]);
            hintBox = r.textBox(ctx, hint, hintSize);
            hintLeft = std::round(hintX - (float)hintBox.w);
        } else {
            std::snprintf(hint, sizeof(hint), "[%d]", 8 - i);
            hintBox = r.textBox(ctx, hint, hintSize);
            hintLeft = std::round(hintX + segW * -0.5f - (float)(hintBox.w / 2));
        }
        r.drawTextBox(ctx, hint, hintBox, hintLeft, std::round((float)y - (float)(hintBox.h / 2)), hintSize, 0xffffff);

        // Clicks always reach statAction (U/M queue builds); only upgradable rows show a pointer.
        if (hasPlayer) {
            hitBoxes.push_back({zoneX, zoneY, zoneW, segH, ACTION_STAT, i, canUpgrade ? CURSOR_POINTER : CURSOR_DEFAULT});
        }
        maxLen = std::max(maxLen, barLen);
        ++rows;
    }

    // "xN" hangs off the top of the longest bar, rotated -22.5deg about its corner.
    if ((int)cam.statsAvailable >= 2) {
        char buf[16];
        std::snprintf(buf, sizeof(buf), "x%d", (int)cam.statsAvailable);
        double size = (double)unit(24);
        GameRendering::TextBox box = game.rendering.textBox(ctx, buf, size);
        ctx.save();
        ctx.translate(maxLen + xf, (float)h + unit(-10) - u30 + (float)rows * unit(-24) + unit(15) - (float)(box.h / 2));
        ctx.rotate(-0.3926991);
        game.rendering.drawTextBox(ctx, buf, box, 0, 0, size, 0xffffff);
        ctx.restore();
    }
    ctx.restore();
}

// ---- Class upgrades ----

void GameUI::drawButton(cp5::Context& ctx, double x, double y, double w, double h, uint32_t rgb,
                        bool enabled, bool hover, bool pressed, const std::string& label, double ui,
                        uint32_t borderColor, double borderWidth) {
    uint32_t fill = pastel(rgb);
    if (pressed) fill = blendChannel3(fill, 0.8f, 0.0f);
    else if (enabled && hover) fill = blendChannel3(fill, 0.8f, 51.0f);
    // Drawn into its own padded canvas and blitted at trunc(x - pad), so the button lands on
    // whole pixels.
    float pad = (float)borderWidth + 5.0f;
    x = (int)((float)x - pad) + pad;
    y = (int)((float)y - pad) + pad;

    ctx.save();
    ctx.beginPath();
    ctx.rect(x, y, w, h);
    ctx.setLineWidth(borderWidth * ui);
    ctx.setLineJoin(cp5::LineJoin::round);
    ctx.setStrokeColor(borderColor);
    ctx.stroke();
    ctx.setFillColor(fill);
    ctx.fillRect(x, y, w, h);

    ctx.setAlpha(ctx.alpha() * 0.2);
    ctx.setFillColor(0x000000);
    if (pressed) ctx.fillRect(x, y, w, h * 7 / 12);
    else ctx.fillRect(x, y + h * 7 / 12, w, h * 5 / 12);
    ctx.restore();

    if (!label.empty()) {
        GameRendering& r = Game::get().rendering;
        GameRendering::TextBox box = r.textBox(ctx, label, h * 0.56);
        r.drawTextBox(ctx, label, box, x + std::round((float)w * 0.5f - (float)(box.w / 2)),
                      y + std::round((float)h * 0.5f - (float)(box.h / 2)), h * 0.56, 0xffffff);
    }
}

void GameUI::renderUpgrades(cp5::Context& ctx, Game& game, double now, double ui, double mx, double my) {
    Entity* camera = game.simulation.getCamera();
    CameraData* cam = camera && camera->camera ? &*camera->camera : nullptr;
    bool hasPlayer = cam && game.simulation.get(cam->player);
    float dt = game.rendering.dt();
    double w = cp5::canvasWidth(), h = cp5::canvasHeight();

    int ids[32];
    int count = cam ? tankdefs::upgrades(cam->tank, ids, 32) : 0;
    int level = cam ? (int)cam->level : 0;

    if (cam && upgradesIgnored) {
        if (level >= ignoreUntilLevel) upgradesIgnored = false;
        if (mx < 30 * ui && my < h / 2) upgradesIgnored = false;
    }

    auto inList = [&](int id) {
        for (int i = 0; i < count; ++i) if (ids[i] == id) return true;
        return false;
    };
    auto slotAvail = [&](const UpgradeSlot& s) {
        return hasPlayer && !upgradesIgnored && inList(s.tankId) && level >= tankdefs::levelRequirement(s.tankId);
    };

    if (hasPlayer && !upgradesIgnored) {
        for (int i = 0; i < count; ++i) {
            if (level < tankdefs::levelRequirement(ids[i])) continue;
            bool have = false;
            for (const UpgradeSlot& s : upgradeSlots) if (s.tankId == ids[i]) { have = true; break; }
            if (have) continue;
            UpgradeSlot slot;
            slot.tankId = ids[i];
            slot.color = teamColorHex(i & 7);
            upgradeSlots.push_back(slot);
        }
    }

    if (upgradeSlots.empty()) { upgradeLastFade = 0; return; }

    float uf = GameRendering::uiBase();
    auto unit = GameRendering::uiUnits;
    float slotLeft = unit(200), slotTop = unit(75), slotShift = unit(100), box = unit(95), gap = unit(15),
          colStep = unit(115);

    float maxFade = 0;
    bool anyAvailable = false;
    float maxColX = -slotShift, maxBottom = slotTop;

    if (!upgradeLayer) upgradeLayer = std::make_unique<cp5::Context>(true);
    upgradeLayer->beginLayer((int)w, (int)h);
    cp5::Context& layer = *upgradeLayer;
    double nameSize = (double)unit(14);

    for (size_t i = 0; i < upgradeSlots.size();) {
        UpgradeSlot& slot = upgradeSlots[i];
        bool avail = slotAvail(slot);
        slot.fade = approach(slot.fade, avail ? 1.0f : 0.0f, dt, 0.1f);
        if (!avail && slot.fade < 0.001f) { upgradeSlots.erase(upgradeSlots.begin() + (long)i); continue; }
        if (avail) anyAvailable = true;
        maxFade = std::max(maxFade, slot.fade);

        if (slot.col < 0) {
            auto taken = [&](int col, int row) {
                for (const UpgradeSlot& s : upgradeSlots)
                    if (s.col == col && s.row == row && slotAvail(s)) return true;
                return false;
            };
            for (int row = 0;; ++row) {
                if (!taken(0, row)) { slot.col = 0; slot.row = row; break; }
                if (!taken(1, row)) { slot.col = 1; slot.row = row; break; }
            }
        }

        float cx = slot.fade * ((float)slot.col * colStep + slotLeft) - slotShift;
        float top = (box + gap) * (float)slot.row + slotTop;
        float bottom = top + box;

        bool over = mx >= cx - box / 2 && mx <= cx + box / 2 && my >= top && my <= bottom;
        bool pressed = over && pressedButton(ACTION_UPGRADE, slot.tankId);
        drawButton(layer, cx - box * 0.5f, top, box, box, slot.color, slot.fade > 0.9f && avail, over, pressed, "", ui);
        game.rendering.drawTankPreview(layer, slot.tankId, cx, top + box / 2, now / 3000.0, 0.4 * ui, true);

        // The name's box sits on the slot's bottom edge, unrounded.
        const std::string& name = tankdefs::name(slot.tankId);
        GameRendering::TextBox nameBox = game.rendering.textBox(layer, name, nameSize);
        game.rendering.drawTextBox(layer, name, nameBox, cx - (float)(nameBox.w / 2), bottom - (float)nameBox.h,
                                   nameSize, 0xffffff);

        addButton(cx - box / 2, top, box, box, ACTION_UPGRADE, slot.tankId, slot.fade > 0.9f && avail);
        maxColX = std::max(maxColX, cx);
        // Fading-out rows must not pin the Ignore anchor while they decay.
        if (avail) maxBottom = std::max(maxBottom, bottom + gap);
        ++i;
    }

    // The width anchor eases toward the previous frame's fade. Both snap when the panel was
    // hidden.
    float lastFade = upgradeLastFade;
    upgradeLastFade = maxFade < 0.01f ? 0.0f : maxFade;
    if (maxFade < 0.01f) return;

    float widthTarget = (float)((double)((lastFade * slotLeft - slotShift) / uf) / config::uiScale);
    float bottomTarget = (float)((double)(maxBottom / uf) / config::uiScale);
    if (lastFade == 0.0f) {
        upgradeWidthAnim = widthTarget;
        upgradeBottomAnim = bottomTarget;
    } else {
        double k = clamp01(1.0 - std::pow(0.9, dt / 16.666667));
        upgradeWidthAnim = (float)(k * widthTarget + (1.0 - k) * upgradeWidthAnim);
        if (anyAvailable) upgradeBottomAnim = (float)(k * bottomTarget + (1.0 - k) * upgradeBottomAnim);
    }
    float ignoreX = (maxColX + unit(upgradeWidthAnim)) * 0.5f;
    float ignoreY = unit(upgradeBottomAnim);
    float ignoreW = unit(80), ignoreH = unit(25), ignoreLeft = ignoreX + ignoreW * -0.5f;

    double titleSize = (double)unit(24);
    GameRendering::TextBox titleBox = game.rendering.textBox(layer, "Upgrades", titleSize);
    game.rendering.drawTextBox(layer, "Upgrades", titleBox, std::round(ignoreX - (float)(titleBox.w / 2)),
                               std::round(unit(40) - (float)(titleBox.h / 2)), titleSize, 0xffffff);

    bool ignoreEnabled = maxFade > 0.9f && anyAvailable;
    bool ignoreOver = mx >= ignoreLeft && mx <= ignoreLeft + ignoreW && my >= ignoreY && my <= ignoreY + ignoreH;
    bool ignorePressed = ignoreOver && pressedButton(ACTION_IGNORE, 0);
    drawButton(layer, ignoreLeft, ignoreY, ignoreW, ignoreH, 0x777777, ignoreEnabled, ignoreOver, ignorePressed, "Ignore", ui);
    addButton(ignoreLeft, ignoreY, ignoreW, ignoreH, ACTION_IGNORE, 0, ignoreEnabled);

    ctx.setAlpha(0.9);
    layer.compositeLayer(ctx);
    ctx.setAlpha(1);
}

// ---- Class tree ----

int GameUI::buildTreeNode(int tankId, int sibling, int depth) {
    int index = (int)treeNodes.size();
    treeNodes.emplace_back();
    treeNodes[index].tankId = tankId;
    treeNodes[index].level = tankdefs::levelRequirement(tankId);
    treeNodes[index].sibling = sibling;
    if (depth >= 16) return index;
    int upgrades[64];
    int count = tankdefs::upgrades(tankId, upgrades, 64);
    for (int i = 0; i < count; ++i) {
        int child = buildTreeNode(upgrades[i], i, depth + 1);
        treeNodes[index].children.push_back(child);
    }
    return index;
}

// One ring sector, spun in from -pi and grown with the fade (deeper rings lag behind).
void GameUI::drawTreeSector(cp5::Context& ctx, Game& game, int node, double a0, double a1, int inner, int outer,
                            uint32_t color, double radius, double iconScale, int index) {
    float l = (float)(inner / 15.0 + -1.0) + 1.0f;
    ctx.save();
    ctx.scaleAlpha(std::pow((double)treeFade, (double)l * 0.3));
    float p = std::pow(treeFade, l);
    float r = p * (float)radius;
    float spin = (float)((1.0f - p) * -3.141593);
    float start = spin + (float)a0, end = spin + (float)a1;
    float innerR = (float)inner / 60.0f * r, outerR = (float)outer / 60.0f * r;
    ctx.beginPath();
    ctx.arc(0, 0, innerR, start, end, false);
    ctx.lineTo(outerR * std::cos(end), outerR * std::sin(end));
    ctx.arc(0, 0, outerR, end, start, true);
    ctx.closePath();
    ctx.setFillColor(color);
    ctx.fill();
    double w = cp5::canvasWidth(), h = cp5::canvasHeight();
    ctx.setLineWidth(p * config::uiScale * std::max(w / 1920.0, h / 1080.0) * 5.0);
    ctx.setLineJoin(cp5::LineJoin::round);
    ctx.setStrokeColor(0x555555);
    ctx.stroke();
    if (node >= 0) {
        TreeNode& n = treeNodes[node];
        float mid = (start + end) * 0.5f, midR = (innerR + outerR) * 0.5f;
        ctx.translate(midR * std::cos(mid), midR * std::sin(mid));
        ctx.rotate(mid);
        ctx.scale(p, p);
        // The icon cache only re-renders every 15th frame after the size changes.
        if (n.iconScale != iconScale) { n.iconScale = iconScale; n.iconReady = false; }
        if (!n.iconReady && (treeFrame + index) % 15 == 0) n.iconReady = true;
        if (n.iconReady) game.rendering.drawTankPreview(ctx, n.tankId, 0, 0, 0, iconScale, true);
    }
    ctx.restore();
}

void GameUI::renderClassTree(cp5::Context& ctx, Game& game, double now, bool hasPlayer) {
    bool held = game.keyHeld(Key::Y) && hasPlayer;
    if (held) {
        treeFade = (treeFade * 9.0f + 1.0f) / 10.0f;
    } else {
        if (treeFade > 0.99f) treeFade = 0.99f;
        treeFade = (float)std::pow((double)treeFade, 1.25);
    }
    if (treeFade < 0.01f) return;

    if (!treeBuilt) {
        if (tankdefs::count() == 0) return;
        treeBuilt = true;
        treeNodes.clear();
        buildTreeNode(0, 0, 0);
        int leaves = 0;
        for (const TreeNode& n : treeNodes) leaves += n.children.empty();
        // Leaves get equal slices in depth-first order; parents span their children (post-order).
        int at = 0;
        std::vector<int> post;
        auto walk = [&](auto& self, int i) -> void {
            for (int c : treeNodes[i].children) self(self, c);
            TreeNode& n = treeNodes[i];
            if (n.children.empty()) {
                double slice = 6.283185 / (double)leaves;
                n.start = (float)(slice * at);
                n.end = (float)(slice * (at + 1));
                ++at;
            } else {
                n.start = treeNodes[n.children.front()].start;
                n.end = treeNodes[n.children.back()].end;
            }
            post.push_back(i);
        };
        walk(walk, 0);
        treeOrder = post;
        std::stable_sort(treeOrder.begin(), treeOrder.end(),
                         [&](int a, int b) { return treeNodes[a].level > treeNodes[b].level; });
        treeFrame = 0;
    }
    ++treeFrame;

    double w = cp5::canvasWidth(), h = cp5::canvasHeight();
    double screen = std::max(w / 1920.0, h / 1080.0);
    double radius = config::uiScale * screen * 400.0;
    double iconScale = screen * 0.25;
    int index = 0;
    ctx.save();
    ctx.translate((double)((int)w / 2), (double)((int)h / 2));
    for (int i : treeOrder) {
        const TreeNode& n = treeNodes[i];
        ctx.save();
        ctx.rotate(now / 15000.0);
        int outer = 60;
        for (int c : n.children) outer = std::min(outer, treeNodes[c].level);
        for (int c : n.children) {
            const TreeNode& child = treeNodes[c];
            if (child.level > outer)
                drawTreeSector(ctx, game, -1, child.start, child.end, outer, child.level, 0x555555, radius, iconScale, index++);
        }
        if (n.level != 0)
            drawTreeSector(ctx, game, i, n.start, n.end, n.level, outer, pastel(teamColorHex(n.sibling)), radius, iconScale, index++);
        ctx.restore();
    }
    ctx.restore();
}

// "Connecting..." under the gamemode buttons while the socket isn't in a game.
void GameUI::renderConnecting(cp5::Context& ctx, Game& game, double ui) {
    float target = game.accepted ? 0.0f : 1.0f;
    connectAlpha = approach(connectAlpha, target, game.rendering.dt(), target > connectAlpha ? 0.05f : 0.1f);
    if (connectAlpha < 0.01) return;
    double w = cp5::canvasWidth(), h = cp5::canvasHeight();
    ctx.save();
    ctx.setAlpha(connectAlpha);
    game.rendering.drawLabel(ctx, "Connecting...", std::round(w / 2), std::round(h / 2), 72 * ui, 0xffffff, true, 0x000000, 0.1);
    ctx.restore();
}

// ---- Console ----

namespace {
    enum class CvarKind { Bool, Int, Color, Real, Text };
    struct Convar {
        const char* name;
        const char* desc;
        CvarKind kind;
        void* ptr;
        std::string value; // unused for Text, which reads the target string directly
    };

    std::vector<Convar>& convars() {
        static std::vector<Convar> table = [] {
            std::vector<Convar> t = {
                {"net_force_secure", "Force connecting via wss", CvarKind::Bool, &config::netForceSecure, ""},
                {"net_latency_history_size", "Number of samples to average latency with", CvarKind::Int, &config::netLatencyHistorySize, ""},
                {"ui_prevent_right_click", "Prevent right click from triggering context menu", CvarKind::Bool, &config::uiPreventRightClick, ""},
                {"ren_ui", "Render UI layer", CvarKind::Bool, &config::renUi, ""},
                {"ren_background", "Render background (and grid)", CvarKind::Bool, &config::renBackground, ""},
                {"ren_solid_background", "Render background as a solid color, without the grid", CvarKind::Bool, &config::renSolidBackground, ""},
                {"ren_cache_grid", "Cache grid rendering on a separate canvas", CvarKind::Bool, &config::renCacheGrid, ""},
                {"ren_names", "Render names", CvarKind::Bool, &config::renNames, ""},
                {"ren_scoreboard", "Render scoreboard", CvarKind::Bool, &config::renScoreboard, ""},
                {"ren_scoreboard_names", "Render scoreboard names", CvarKind::Bool, &config::renScoreboardNames, ""},
                {"ren_health_bars", "Render health bars", CvarKind::Bool, &config::renHealthBars, ""},
                {"ren_achievements", "Render achievements", CvarKind::Bool, &config::renAchievements, ""},
                {"ren_upgrades", "Render class upgrades", CvarKind::Bool, &config::renUpgrades, ""},
                {"ren_changelog", "Render changelog", CvarKind::Bool, &config::renChangelog, ""},
                {"ren_stroke_soft_color", "Renders strokes as a darker shade of the fill color", CvarKind::Bool, &config::renStrokeSoftColor, ""},
                {"ren_stroke_soft_color_intensity", "How much to blend black with the fill color (0 = nothing, 1 = black)", CvarKind::Real, &config::strokeIntensity, ""},
                {"ren_raw_health_values", "Render raw health bar values", CvarKind::Bool, &config::renRawHealthValues, ""},
                {"ren_fps", "Render FPS", CvarKind::Bool, &config::renFps, ""},
                {"ren_ui_scale", "", CvarKind::Real, &config::uiScale, ""},
                {"ren_debug_info", "Render some debug info on the server stats text", CvarKind::Bool, &config::renDebugInfo, ""},
                {"ren_debug_collisions", "Render Collidable debug info (those are not hitboxes)", CvarKind::Bool, &config::renDebugCollisions, ""},
                {"ren_context_reinitialization", "Reinitialize contexts if FPS is too low (bad browser heuristics?)", CvarKind::Bool, &config::renContextReinitialization, ""},
                {"ren_minimap_viewport", "Render viewable area as a rectangle on the minimap", CvarKind::Bool, &config::renMinimapViewport, ""},
                {"ren_pattern_grid", "Use canvas createPattern for grid, it's faster but looks slightly worse", CvarKind::Bool, &config::renPatternGrid, ""},
                {"ren_health_background_color", "Color for the health bar background", CvarKind::Color, &config::healthBackgroundColor, ""},
                {"ren_health_fill_color", "Color for the health bar filling", CvarKind::Color, &config::healthFillColor, ""},
                {"ren_grid_base_alpha", "Base alpha for the grid", CvarKind::Real, &config::gridBaseAlpha, ""},
                {"ren_grid_color", "Grid color", CvarKind::Color, &config::gridColor, ""},
                {"ren_background_color", "Base color for the background", CvarKind::Color, &config::backgroundColor, ""},
                {"ren_stroke_solid_color", "Color for solid borders", CvarKind::Color, &config::strokeSolidColor, ""},
                {"ren_minimap_background_color", "Background color for the minimap", CvarKind::Color, &config::minimapBackgroundColor, ""},
                {"ren_minimap_border_color", "Border color for the minimap", CvarKind::Color, &config::minimapBorderColor, ""},
                {"ren_stats", "Render stat upgrades", CvarKind::Bool, &config::renStats, ""},
                {"ren_border_color", "Color for the area outside the map", CvarKind::Color, &config::borderColor, ""},
                {"ren_border_color_alpha", "Alpha for the area outside the map", CvarKind::Real, &config::borderAlpha, ""},
                {"ren_xp_bar_fill_color", "Fill color for XP bar", CvarKind::Color, &config::xpBarFillColor, ""},
                {"ren_score_bar_fill_color", "Fill color for score bar", CvarKind::Color, &config::scoreBarFillColor, ""},
                {"ren_bar_background_color", "Background color for a lot of bars in game", CvarKind::Color, &config::barBackgroundColor, ""},
                {"net_predict_movement", "Enable clientside prediction for movement", CvarKind::Bool, &config::netPredictMovement, ""},
                {"game_stats_build", "Stat build to aim for, for example: 555667", CvarKind::Text, &config::gameStatsBuild, ""},
            };
            // Defaults print like std::to_string (0.250000, 5592405).
            for (Convar& c : t) {
                switch (c.kind) {
                    case CvarKind::Bool: c.value = *(bool*)c.ptr ? "true" : "false"; break;
                    case CvarKind::Int: c.value = std::to_string(*(int*)c.ptr); break;
                    case CvarKind::Color: c.value = std::to_string((int)*(uint32_t*)c.ptr); break;
                    case CvarKind::Real: c.value = std::to_string(*(double*)c.ptr); break;
                    case CvarKind::Text: break;
                }
            }
            return t;
        }();
        return table;
    }

    Convar* findConvar(const std::string& name) {
        for (Convar& c : convars()) if (name == c.name) return &c;
        return nullptr;
    }

    const std::string& convarValue(const Convar& c) {
        return c.kind == CvarKind::Text ? *(std::string*)c.ptr : c.value;
    }

    // strtol base 0: 0x.. is hex, a leading 0 is octal.
    int parseInt(const std::string& s) { return (int)std::strtoll(s.c_str(), nullptr, 0); }

    // Keeps the raw string and re-parses it; "", "0" and "false" are the only false bools.
    void setConvar(Convar& c, const std::string& value) {
        switch (c.kind) {
            case CvarKind::Bool: *(bool*)c.ptr = !(value.empty() || value == "0" || value == "false"); break;
            case CvarKind::Int: *(int*)c.ptr = parseInt(value); break;
            case CvarKind::Color: *(uint32_t*)c.ptr = (uint32_t)parseInt(value); break;
            case CvarKind::Real: *(double*)c.ptr = std::strtod(value.c_str(), nullptr); break;
            case CvarKind::Text: *(std::string*)c.ptr = value; return;
        }
        c.value = value;
    }

    struct ClientCommand {
        const char* id;
        const char* usage;
        const char* desc;
    };
    const ClientCommand COMMANDS[] = {
        {"ui_replace_colors", "[hex1] [hex2] ...", "Replace colors from the UI color palette table in order"},
        {"help", "", "Print help text"},
        {"lb_reconnect", "", "Disconnect from the current server and find a new server"},
        {"net_replace_color", "[id] [hex]", "Replace a color from the network color table"},
        {"net_replace_colors", "[hex1] [hex2] ...", "Replace colors from the network color table in order"},
        {"con_toggle", "", "Toggles the console on or off"},
        {"game_spawn", "[name]", "Spawns with the given name, if possible"},
        {"util_test", "", "Test command to check if custom commands are working, prints 'Hello World' to the console"},
        {"util_set_dev_password", "[password]", "Sets the dev password (reconnect required)"},
        {"util_change_scheduler", "[?scheduler]", "Changes the game's frame scheduler (default: requestAnimationFrame)"},
        {"util_reload_servers", "[?interval]", "Sets the interval in which gamemodes are reloaded automatically (milliseconds, 'never' or 'connect') or reloads once if no interval is given"},
        {"util_reload_tanks", "[?interval]", "Sets the interval in which tanks are reloaded automatically (milliseconds, 'never' or 'connect') or reloads once if no interval is given"},
        {"util_reload_commands", "[?interval]", "Sets the interval in which commands are reloaded automatically (milliseconds, 'never' or 'connect') or reloads once if no interval is given"},
        {"util_set_changelog", "[line 1\\n] [line 2] ...", "Sets the changelog to the given text, remember to use \\n before and after each line"},
    };

    bool isClientCommand(const std::string& id) {
        for (const ClientCommand& c : COMMANDS) if (id == c.id) return true;
        return false;
    }

    int serverCommandIndex(const std::string& id) {
        int n = tankdefs::commandCount();
        int level = Game::get().commandLevel;
        for (int i = 0; i < n; ++i) if (tankdefs::commandAllowed(i, level) && tankdefs::commandId(i) == id) return i;
        return -1;
    }

    // Whitespace splits tokens; " and ' group (the quotes are dropped).
    std::vector<std::string> tokenize(const std::string& line) {
        std::vector<std::string> out;
        auto space = [](char c) { return c == ' ' || (c >= '\t' && c <= '\r'); };
        size_t i = 0;
        while (i < line.size()) {
            while (i < line.size() && space(line[i])) ++i;
            if (i >= line.size()) break;
            std::string tok;
            while (i < line.size() && !space(line[i])) {
                if (line[i] == '"' || line[i] == '\'') {
                    char q = line[i++];
                    while (i < line.size() && line[i] != q) tok += line[i++];
                    if (i < line.size()) ++i;
                } else {
                    tok += line[i++];
                }
            }
            out.push_back(tok);
        }
        return out;
    }

    std::string helpText() {
        std::string out = "Commands:\n";
        std::vector<std::string> ids;
        for (const ClientCommand& c : COMMANDS) ids.push_back(c.id);
        int sc = tankdefs::commandCount();
        int level = Game::get().commandLevel;
        for (int i = 0; i < sc; ++i) {
            std::string id = tankdefs::commandId(i);
            if (!isClientCommand(id) && tankdefs::commandAllowed(i, level)) ids.push_back(id);
        }
        std::sort(ids.begin(), ids.end());
        for (const std::string& id : ids) {
            std::string usage, desc;
            int si = serverCommandIndex(id);
            if (isClientCommand(id)) {
                for (const ClientCommand& c : COMMANDS) if (id == c.id) { usage = c.usage; desc = c.desc; }
            } else if (si >= 0) {
                usage = tankdefs::commandUsage(si);
                desc = tankdefs::commandDescription(si);
            }
            out += id + ' ' + usage + " # " + desc + '\n';
        }
        out += "\nVariables:\n";
        std::vector<const Convar*> sorted;
        for (const Convar& c : convars()) sorted.push_back(&c);
        std::sort(sorted.begin(), sorted.end(), [](const Convar* a, const Convar* b) {
            return std::string(a->name) < b->name;
        });
        for (const Convar* c : sorted) out += std::string(c->name) + " = " + convarValue(*c) + " # " + c->desc + '\n';
        return out;
    }
}

// window.input: execute, get_convar, set_convar, print_convar_help.
namespace {
    char gInputArgs[2][4096];
}

extern "C" {
EMSCRIPTEN_KEEPALIVE char* input_arg(int i) { return gInputArgs[i & 1]; }

EMSCRIPTEN_KEEPALIVE void input_execute() {
    Game& game = Game::get();
    game.ui.execute(game, gInputArgs[0]);
}

EMSCRIPTEN_KEEPALIVE int input_set_convar() {
    Convar* c = findConvar(gInputArgs[0]);
    if (!c) { std::printf("Couldn't find ConVar \"%s\"\n", gInputArgs[0]); return 0; }
    setConvar(*c, gInputArgs[1]);
    return 1;
}

EMSCRIPTEN_KEEPALIVE const char* input_get_convar() {
    Convar* c = findConvar(gInputArgs[0]);
    if (!c) { std::printf("Couldn't find ConVar \"%s\"\n", gInputArgs[0]); return nullptr; }
    return convarValue(*c).c_str();
}

EMSCRIPTEN_KEEPALIVE void input_print_convar_help() {
    for (const Convar& c : convars()) std::printf("%s = %s # %s\n", c.name, convarValue(c).c_str(), c.desc);
}
}

EM_JS(void, ui_install_input_api, (), {
    const put = (i, v) => stringToUTF8(String(v), _input_arg(i), 4096);
    const input = window.input = window.input || {};
    input.execute = cmd => { put(0, cmd); _input_execute(); };
    input.set_convar = (key, val) => { put(0, key); put(1, val); return !!_input_set_convar(); };
    input.get_convar = key => { put(0, key); const p = _input_get_convar(); return p ? UTF8ToString(p) : null; };
    input.print_convar_help = () => _input_print_convar_help();
});

void installConsoleApi() { ui_install_input_api(); }

void GameUI::toggleConsole() {
    consoleOpen = !consoleOpen;
    consoleLines.clear();
    consoleInput.clear();
    if (textFieldActive == 1) ui_textfield_set("");
}

void GameUI::runCommand(Game& game, const std::string& command, std::string& out) {
    std::vector<std::string> tokens = tokenize(command);
    const std::string cmd = tokens.empty() ? "" : tokens[0];

    if (isClientCommand(cmd)) {
        if (cmd == "help") {
            out += helpText();
        } else if (cmd == "con_toggle") {
            toggleConsole();
        } else if (cmd == "game_spawn") {
            game.sendSpawn(tokens.size() > 1 ? tokens[1] : nameInput);
        } else if (cmd == "lb_reconnect") {
            game.reconnect();
        } else if (cmd == "net_replace_color") {
            if (tokens.size() <= 2) { out += "Not enough arguments!"; return; }
            int id = parseInt(tokens[1]);
            if (id >= NET_COLOR_COUNT) { std::printf("Invalid color index: %d\n", id); return; }
            if (id >= 0) NET_COLORS[id] = (uint32_t)parseInt(tokens[2]) & 0xffffff;
        } else if (cmd == "net_replace_colors") {
            for (size_t i = 1; i < tokens.size(); ++i) {
                int id = (int)i - 1;
                if (id >= NET_COLOR_COUNT) { std::printf("Invalid color index: %d\n", id); continue; }
                NET_COLORS[id] = (uint32_t)parseInt(tokens[i]) & 0xffffff;
            }
        } else if (cmd == "util_test") {
            std::printf("Hello World\n");
        } else if (cmd == "util_set_dev_password") {
            if (tokens.size() > 1 && !tokens[1].empty()) ui_set_dev_password(tokens[1].c_str());
        } else if (cmd == "util_change_scheduler") {
            // setTimeout(loop) runs uncapped; anything else falls back to requestAnimationFrame.
            if (tokens.size() > 1 && tokens[1] == "setTimeout") emscripten_set_main_loop_timing(EM_TIMING_SETTIMEOUT, 0);
            else emscripten_set_main_loop_timing(EM_TIMING_RAF, 1);
        } else if (cmd == "util_reload_servers" || cmd == "util_reload_tanks" || cmd == "util_reload_commands") {
            bool once = tokens.size() < 2 || tokens[1].empty();
            tankdefs::reload(cmd.c_str() + 12, once ? nullptr : tokens[1].c_str());
        } else if (cmd == "util_set_changelog") {
            std::string text;
            for (size_t i = 1; i < tokens.size(); ++i) text += (i > 1 ? " " : "") + tokens[i];
            ui_changelog_set(text.c_str());
            changelogLoaded = false;
        } else if (cmd == "ui_replace_colors") {
            for (size_t i = 1; i < tokens.size() && i <= UI_COLOR_COUNT; ++i)
                UI_COLORS[i - 1] = (uint32_t)parseInt(tokens[i]) & 0xffffff;
        }
        return;
    }
    if (serverCommandIndex(cmd) >= 0) {
        game.sendCommand(cmd, std::vector<std::string>(tokens.begin() + 1, tokens.end()));
        return;
    }
    if (Convar* c = findConvar(cmd)) {
        if (tokens.size() >= 2) setConvar(*c, tokens[1]);
        out += cmd + " = " + convarValue(*c);
        return;
    }
    out += "Couldn't find: " + cmd;
}

// ';' and newlines separate commands outside quotes; outputs concatenate.
std::string GameUI::execute(Game& game, const std::string& line) {
    std::string out, current;
    char quote = 0;
    auto run = [&]() {
        if (!current.empty()) runCommand(game, current, out);
        current.clear();
    };
    for (char c : line) {
        if (quote) {
            if (c == quote) quote = 0;
            current += c;
        } else if (c == '"' || c == '\'') {
            quote = c;
            current += c;
        } else if (c == ';' || c == '\n') {
            run();
        } else {
            current += c;
        }
    }
    run();
    return out;
}

void GameUI::renderFps(cp5::Context& ctx, Game& game, double ui) {
    if (!config::renFps || game.rendering.fps() < 0) return;
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.1f FPS", game.rendering.fps());
    double size = 14 * ui;
    ctx.setFontSize(size);
    ctx.setLineWidth(size * 0.2);
    double tw, th;
    ctx.labelSize(buf, &tw, &th);
    double x = std::round(5 * ui), y = std::round(5 * ui);
    game.rendering.drawLabel(ctx, buf, x + tw / 2, y + th / 2, size, 0xffffff);
}

void GameUI::renderConsole(cp5::Context& ctx, Game& game, double ui, double now) {
    (void)now;
    double w = cp5::canvasWidth(), h = cp5::canvasHeight();
    float dt = game.rendering.dt();
    double margin = 10 * ui, inputH = 20 * ui, lineSize = 14 * ui, titleSize = 20 * ui;
    // Rows step by the unrounded raster height; the title's int raster height sets the layout.
    double lineH = (float)((float)lineSize * 1.4);
    GameRendering& r = game.rendering;
    GameRendering::TextBox titleBox = r.textBox(ctx, "Console", titleSize);
    double titleH = titleBox.h;

    double head = inputH + titleH + (consoleLines.empty() ? margin : 2 * margin);
    double avail = h - 2 * margin - head;
    double panelW = w - 2 * margin;
    double content = lineH * (double)consoleLines.size();
    double visible = content;

    if (avail < content) {
        double delta = wheelAccum * 10 * ui;
        consoleScrollVel += (float)delta;
        consoleScroll += consoleScrollVel;
        consoleScrollVel *= (float)std::pow(0.9, dt / 16.666667);
        float spring = (float)clamp01(1.0 - std::pow(0.8, dt / 16.666667));
        float maxScroll = (float)(content - avail);
        if (consoleScroll > maxScroll) consoleScroll = spring * maxScroll + (1 - spring) * consoleScroll;
        visible = avail;
        if (consoleScroll < 0) consoleScroll = (1 - spring) * consoleScroll;
    } else {
        consoleScroll = 0;
        consoleScrollVel = 0;
    }
    wheelAccum = 0;

    ctx.save();
    ctx.scaleAlpha(0.5);
    ctx.setFillColor(0x000000);
    ctx.fillRect(margin, margin, panelW, head + visible);
    ctx.restore();

    // Output lines: translated under the input box, clipped to the free area.
    ctx.save();
    ctx.translate(2 * margin, margin + titleH + inputH + margin);
    ctx.beginPath();
    ctx.rect(0, 0, panelW, avail);
    ctx.clip();
    double y = -consoleScroll;
    for (const std::string& line : consoleLines) {
        if (!line.empty() && y + lineH > 0 && y < avail)
            r.drawTextBox(ctx, line, r.textBox(ctx, line, lineSize), 0, std::round((float)y), lineSize, 0xffffff);
        y += lineH;
    }
    ctx.restore();

    r.drawTextBox(ctx, "Console", titleBox, std::round(margin), std::round(margin), titleSize, 0xffffff);

    // Input box: white with a 2u black border; the overlaid DOM input renders the text.
    double bx = 2 * margin, by = margin + titleH, bw = w - 4 * margin;
    ctx.setFillColor(0xffffff);
    ctx.fillRect(bx, by, bw, inputH);
    ctx.setStrokeColor(0x000000);
    ctx.setLineWidth(2 * ui);
    ctx.setLineJoin(cp5::LineJoin::round);
    ctx.strokeRect(bx, by, bw, inputH);
    hitBoxes.push_back({bx, by, bw, inputH, 0, 0, CURSOR_TEXT});
    tfX = bx; tfY = by; tfW = bw; tfH = inputH;
    tfFont = inputH * 0.9; tfColor = 0x000000; tfCentered = false; tfOutline = false;
}

// ---- Input ----

// Enter and Start work once a screen's fade passes 0.2. The name box only takes typing from
// 0.7, so before that the stored name is used.
void GameUI::submitSpawn(Game& game) {
    if (textFieldActive == 2) ui_textfield_read_into(nameInput);
    nameInput = trim(nameInput);
    if (!nameInput.empty()) ui_save_name(nameInput.c_str());
    game.sendSpawn(nameInput);
}

// Gamepad: holding X (keys 1-4) or A (keys 5-8) with a d-pad direction upgrades a stat.
// (Start held confirms the death and spawn screens in render().)
void GameUI::padInput(Game& game, bool hasPlayer) {
    if (!game.padActive() || !hasPlayer || !config::renStats) return;
    static const int hold[2] = {2, 0}, dpad[4] = {12, 15, 13, 14};
    for (int k = 1; k <= 8; ++k) {
        if (game.padHeld[hold[(k - 1) / 4]] && game.padPressed[dpad[(k - 1) % 4]]) statAction(game, 8 - k);
    }
}

bool GameUI::onKeyDown(int key, Game& game) {
    // All UI keys go through the UI pass, which ren_ui skips.
    if (!config::renUi) return false;
    // keyCode 36 also covers numpad 7 with numlock off.
    if (key == Key::Home) {
        toggleConsole();
        return true;
    }
    // While the console is open the DOM input owns editing; Enter runs the line.
    if (consoleOpen) {
        if (key == Key::Enter) {
            ui_textfield_read_into(consoleInput);
            if (!consoleInput.empty()) {
                std::string out = execute(game, consoleInput);
                // Only the latest output is shown, split per line (no echo of the input).
                consoleLines.clear();
                size_t start = 0;
                for (size_t i = 0; i <= out.size(); ++i) {
                    if (i < out.size() && out[i] != '\n') continue;
                    if (i == out.size() && start == i) break;
                    consoleLines.push_back(out.substr(start, i - start));
                    std::printf("Line: %s\n", consoleLines.back().c_str());
                    start = i + 1;
                }
                consoleInput.clear();
                if (textFieldActive == 1) ui_textfield_set("");
            }
            return true;
        }
        return false;
    }

    // Enter confirms the spawn and death screens on the next UI pass.
    if (key == Key::Enter) {
        enterPressed = true;
        if ((spawnTarget && spawnAlpha > 0.2) || (deathTarget && deathAlpha > 0.2)) return true;
    }
    if (spawnAlpha > 0.7) {
        // Consume Escape so the browser doesn't revert the field (Tab never gets here).
        if (key == Key::Escape) return true;
        return false;
    }

    // Digits 1-8 on either row (numpad ones only with numlock on, as keyCodes 97-104).
    int digit = 0;
    if (key >= Key::Digit1 && key < Key::Digit1 + 8) digit = key - Key::Digit1 + 1;
    else if (key >= Key::Numpad1 && key < Key::Numpad1 + 8) digit = key - Key::Numpad1 + 1;
    if (digit && config::renStats) {
        statAction(game, 8 - digit);
        return true;
    }
    return false;
}

void GameUI::addButton(double x, double y, double w, double h, int action, int arg, bool enabled) {
    hitBoxes.push_back({x, y, w, h, enabled ? action : 0, arg, enabled ? CURSOR_POINTER : CURSOR_DEFAULT});
}

// Later elements draw on top, so the last one containing the point wins.
const GameUI::HitBox* GameUI::hitAt(double x, double y) const {
    for (auto it = hitBoxes.rbegin(); it != hitBoxes.rend(); ++it) {
        if (x >= it->x && y >= it->y && x < it->x + it->w && y < it->y + it->h) return &*it;
    }
    return nullptr;
}

EM_JS(void, ui_set_cursor, (int cursor), {
    document.getElementById("canvas").style.cursor = ["default", "pointer", "text"][cursor - 1];
});

void GameUI::updateHover(double mx, double my) {
    const HitBox* hb = hitAt(mx, my);
    hoverBlocks = hb != nullptr;
    int want = hb ? hb->cursor : CURSOR_DEFAULT;
    if (want != cursor) {
        cursor = want;
        ui_set_cursor(want);
    }
}

// Press latches the element under the cursor; the action fires on release over it.
bool GameUI::onMouseDown(Game& game) {
    double dpr = cp5::pixelRatio();
    const HitBox* hb = hitAt(game.mouseScreenX * dpr, game.mouseScreenY * dpr);
    if (!hb || !hb->action) return false;
    pressActive = true;
    pressedAction = hb->action;
    pressedArg = hb->arg;
    return true;
}

bool GameUI::onMouseUp(Game& game) {
    if (!pressActive) return false;
    pressActive = false;
    double dpr = cp5::pixelRatio();
    if (const HitBox* top = hitAt(game.mouseScreenX * dpr, game.mouseScreenY * dpr)) {
        const HitBox& hb = *top;
        if (hb.action != pressedAction || hb.arg != pressedArg) return false;
        if (hb.action == ACTION_STAT) statAction(game, hb.arg);
        else if (hb.action == ACTION_UPGRADE) game.sendTankUpgrade(hb.arg);
        else if (hb.action == ACTION_GAMEMODE) game.switchGamemode(tankdefs::serverGamemode(hb.arg));
        else if (hb.action == ACTION_PARTY) ui_copy_text(game.partyLink().c_str());
        else if (hb.action == ACTION_IGNORE) {
            Entity* camera = game.simulation.getCamera();
            if (camera && camera->camera) {
                int level = (int)camera->camera->level;
                upgradesIgnored = true;
                ignoreUntilLevel = level - level % 15 + 15;
            }
        }
        return true;
    }
    return false;
}

bool GameUI::onWheel(double notches) {
    if (!config::renUi) return false;
    wheelAccum += notches;
    return true;
}
