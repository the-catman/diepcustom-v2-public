#pragma once

#include <string>
#include <vector>

// Draw commands queue here and execute in one JS call per frame (cp5::flush).
namespace cp5 { namespace detail {

enum Op {
    opSave = 0, opRestore, opTransform, opTranslate, opRotate, opScale,
    opBegin, opClose, opMove, opLine, opArc, opRect,
    opFill, opStroke, opFillRect, opStrokeRect,
    opClear, opClearRegion, opSetSize,
    opDraw, opDrawSub,
    opFillText, opStrokeText, opOutlineText,
    opClip, opFillPattern
};

extern std::vector<double> cmdBuf;
// Per-frame upkeep of the measureText cache (Text.cpp).
void textFrame();
extern std::vector<char> strBuf;

inline void cmd(int op, int ctx) { cmdBuf.push_back(op); cmdBuf.push_back(ctx); }
inline void arg(double v) { cmdBuf.push_back(v); }
inline size_t str(const std::string& s) {
    size_t off = strBuf.size();
    strBuf.insert(strBuf.end(), s.begin(), s.end());
    return off;
}

} }
