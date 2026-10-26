#include "cp5/Context.h"
#include "cp5/Commands.h"

#include <cstdint>
#include <string>
#include <unordered_map>

#include <emscripten/emscripten.h>

EM_JS(double, cp5_measure, (int id, const char* t, double size), {
    var M = Module.cp5;
    var c = M.ctxs[id];
    var stk = M.st[id];
    var font = M.font(size);
    stk[stk.length - 1].fo = size;
    if (c.font !== font) c.font = font;
    return c.measureText(UTF8ToString(t)).width;
});

// Bumped by the page whenever a font finishes loading; widths measured before are stale.
EM_JS(int, cp5_font_generation, (), {
    const M = Module.cp5;
    if (M.fontGeneration === undefined) {
        M.fontGeneration = 0;
        document.fonts.addEventListener("loadingdone", () => ++M.fontGeneration);
    }
    return M.fontGeneration;
});

namespace cp5 {
using namespace detail;

namespace {
    // Widths by (font px as the font string quantizes it, text). Entries unused for a whole
    // generation of frames drop out, so changing strings (scores) don't pile up.
    struct MeasureCache {
        std::unordered_map<std::string, double> current, previous;
        int frames = 0;
        int fontGeneration = -1;
    } measured;

    constexpr int MEASURE_GENERATION_FRAMES = 600;
}

namespace detail {
    void textFrame() {
        int generation = cp5_font_generation();
        if (generation != measured.fontGeneration) {
            measured.fontGeneration = generation;
            measured.current.clear();
            measured.previous.clear();
            measured.frames = 0;
        } else if (++measured.frames >= MEASURE_GENERATION_FRAMES) {
            measured.previous = std::move(measured.current);
            measured.current.clear();
            measured.frames = 0;
        }
    }
}

double Context::measureText(const std::string& text) {
    int32_t px = (int32_t)(state.fontSize * 2048);
    std::string key((const char*)&px, sizeof(px));
    key += text;
    auto it = measured.current.find(key);
    if (it != measured.current.end()) return it->second;
    double width;
    auto old = measured.previous.find(key);
    if (old != measured.previous.end()) width = old->second;
    else width = cp5_measure(target, text.c_str(), state.fontSize);
    measured.current.emplace(std::move(key), width);
    return width;
}

void Context::addText(const std::string& text, double x, double y, bool stroked) {
    // Generous: glyphs overhang their advance and every baseline sits within 1.5em of them.
    double size = state.fontSize, pad = stroked ? state.lineWidth : 0;
    addLocal(x - size - pad, y - 1.5 * size - pad, x + measureText(text) + size + pad, y + 1.5 * size + pad, 0);
}

void Context::fillText(const std::string& text, double x, double y) {
    if (text.empty() || state.fontSize <= 0) return;
    if (tracking) addText(text, x, y, false);
    size_t off = str(text);
    cmd(opFillText, target); arg(off); arg(text.size()); arg(x); arg(y);
    arg(state.fill); arg(state.alpha); arg(state.fontSize); arg((int)state.baseline);
}

void Context::strokeText(const std::string& text, double x, double y) {
    if (text.empty() || state.fontSize <= 0) return;
    if (tracking) addText(text, x, y, true);
    size_t off = str(text);
    cmd(opStrokeText, target); arg(off); arg(text.size()); arg(x); arg(y);
    arg(state.stroke); arg(state.alpha); arg(state.fontSize); arg((int)state.baseline); arg(state.lineWidth);
}

void Context::fillStrokeText(const std::string& text, double x, double y) {
    if (text.empty() || state.fontSize <= 0) return;
    if (tracking) addText(text, x, y, true);
    size_t off = str(text);
    cmd(opOutlineText, target); arg(off); arg(text.size()); arg(x); arg(y);
    arg(state.fill); arg(state.stroke); arg(state.alpha); arg(state.fontSize); arg((int)state.baseline); arg(state.lineWidth);
}

void Context::labelSize(const std::string& text, double* w, double* h) {
    *w = *h = 0;
    if (text.empty() || state.fontSize <= 0) return;
    double margin = (double)(int)(state.lineWidth * 2);
    *w = measureText(text) + 2 * margin;
    *h = state.fontSize * 1.4;
}

} // namespace cp5
