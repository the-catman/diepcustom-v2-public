#include "Const/Enums.h"
#include "Config.h"

#include <cmath>
#include <unordered_map>

uint32_t colorHex(int color) {
    static const std::unordered_map<int, uint32_t> table = {
        {Color::Border, 0x555555}, {Color::Barrel, 0x999999}, {Color::Tank, 0x00b2e1},
        {Color::TeamBlue, 0x00b2e1}, {Color::TeamRed, 0xf14e54}, {Color::TeamPurple, 0xbf7ff5},
        {Color::TeamGreen, 0x00e16e}, {Color::Shiny, 0x8aff69}, {Color::EnemySquare, 0xffe869},
        {Color::EnemyTriangle, 0xfc7677}, {Color::EnemyPentagon, 0x768dfc}, {Color::EnemyCrasher, 0xf177dd},
        {Color::Neutral, 0xffe869}, {Color::ScoreboardBar, 0x43ff91}, {Color::Box, 0xbbbbbb},
        {Color::EnemyTank, 0xf14e54}, {Color::NecromancerSquare, 0xfcc376}, {Color::Fallen, 0xc0c0c0},
        {Color::kMaxColors, 0x000000},
    };
    auto it = table.find(color);
    return it == table.end() ? 0 : it->second;
}

std::vector<double> levelToScoreTable;

void initLevelToScoreTable() {
    levelToScoreTable.assign(config::maxPlayerLevel, 0.0);
    for (int i = 1; i < config::maxPlayerLevel; ++i) {
        levelToScoreTable[i] = levelToScoreTable[i - 1] + (40.0 / 9 * std::pow(1.06, i - 1) * std::min(31, i));
    }
}

double levelToScore(int level) {
    if (level >= config::maxPlayerLevel) return levelToScoreTable[config::maxPlayerLevel - 1];
    if (level <= 0) return 0;
    return levelToScoreTable[level - 1];
}
