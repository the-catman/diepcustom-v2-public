#include "Const.h"

namespace config {
    const uint32_t MAGIC_NUM = computeMagicNum(BUILD_HASH);
    double uiScale = 1.0;
    double gridBaseAlpha = 0.1;
    uint32_t gridColor = 0x000000;
    uint32_t backgroundColor = 0xcdcdcd;
    uint32_t borderColor = 0x000000;
    double borderAlpha = 0.1;
    double strokeIntensity = 0.25;
    uint32_t healthBackgroundColor = 0x555555;
    uint32_t healthFillColor = 0x85e37d;
    uint32_t xpBarFillColor = 0xffde43;
    uint32_t scoreBarFillColor = 0x43ff91;
    uint32_t minimapBackgroundColor = 0xcdcdcd;
    uint32_t minimapBorderColor = 0x555555;
    uint32_t strokeSolidColor = 0x555555;
    uint32_t barBackgroundColor = 0x000000;
    bool renUi = true;
    bool renBackground = true;
    bool renSolidBackground = false;
    bool renCacheGrid = true;
    bool renNames = true;
    bool renScoreboard = true;
    bool renScoreboardNames = true;
    bool renHealthBars = true;
    bool renAchievements = true;
    bool renUpgrades = true;
    bool renChangelog = true;
    bool renStrokeSoftColor = true;
    bool renRawHealthValues = false;
    bool renFps = true;
    bool renDebugInfo = false;
    bool renDebugCollisions = false;
    bool renContextReinitialization = false;
    bool renMinimapViewport = false;
    bool renPatternGrid = true;
    bool renStats = true;
    bool netPredictMovement = false;
    bool netForceSecure = false;
    int netLatencyHistorySize = 25;
    bool uiPreventRightClick = true;
    std::string gameStatsBuild;
    bool renWorldGL = true;
}

// Network color table (19 entries, patched by net_replace_color[s]).
uint32_t NET_COLORS[NET_COLOR_COUNT] = {
    0x555555, 0x999999, 0x00b2e1, 0x00b2e1, 0xf14e54, 0xbf7ff5, 0x00e16e, 0x8aff69, 0xffe869, 0xfc7677,
    0x768dfc, 0xf177dd, 0xffe869, 0x43ff91, 0xbbbbbb, 0xf14e54, 0xfcc376, 0xc0c0c0, 0x000000,
};

// UI palette (stat/upgrade team colors), patched by ui_replace_colors.
uint32_t UI_COLORS[UI_COLOR_COUNT] = {
    0x43fff9, 0x82ff43, 0xff4343, 0xffde43, 0x437fff, 0x8543ff, 0xf943ff, 0xfcad76
};

uint32_t colorHex(int color) {
    if (color < 0 || color >= NET_COLOR_COUNT) return 0x000000;
    return NET_COLORS[color];
}

uint32_t teamColorHex(int index) {
    if (index < 0) return 0x777777;
    return UI_COLORS[index & 7];
}

uint32_t computeMagicNum(const std::string& build) {
    int64_t res = 0, seed = 1, timer = 0;
    for (int i = 0; i < 40; ++i) {
        int c = 0;
        char ch = build[i];
        if (ch >= '0' && ch <= '9') c = ch - '0';
        else if (ch >= 'a' && ch <= 'f') c = ch - 'a' + 10;
        else if (ch >= 'A' && ch <= 'F') c = ch - 'A' + 10;
        res ^= ((int64_t)c << ((seed & 1) << 2)) << (timer << 3);
        res &= 0xffffffff;
        timer = (timer + 1) & 3;
        seed ^= (timer == 0) ? 1 : 0;
    }
    return (uint32_t)(res & 0xffffffff);
}
