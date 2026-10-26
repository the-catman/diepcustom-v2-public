#include "cp5/Context.h"
#include "cp5/Commands.h"

#include <algorithm>
#include <cmath>

#include <emscripten/emscripten.h>
#include <emscripten/html5.h>

// ---- JS Canvas2D registry ----

EM_JS(int, cp5_boot, (const char* sel), {
    var canvas = document.querySelector(UTF8ToString(sel));
    if (!canvas) return 0;
    var overlay = document.querySelector("#overlay");
    var baseContext = canvas.getContext("2d", { alpha: false });
    var overlayContext = overlay ? overlay.getContext("2d", { alpha: true }) : baseContext;
    Module.cp5 = {
        ctxs: [baseContext],
        hex: function(c) { return "#" + (c & 0xffffff).toString(16).padStart(6, "0"); },
        font: function(s) { return ((s * 2048) | 0) / 2048 + "px Ubuntu"; },
        // Mirrored style state per ctx so the interpreter skips redundant assignments.
        mkst: function() { return [{f:-1,s:-1,a:-1,lw:-1,lj:-1,lc:-1,ml:-1,fo:-1,tb:-1}]; },
        baseline: ["top", "hanging", "middle", "alphabetic", "ideographic", "bottom"],
        join: ["round", "bevel", "miter"],
        cap: ["butt", "round", "square"]
    };
    var M = Module.cp5;
    M.layers = [{ctx: baseContext, st: M.mkst()}, {ctx: overlayContext, st: M.mkst()}];
    M.activeLayer = 0;
    M.st = [M.layers[0].st];
    document.fonts.load("1px Ubuntu");
    return 1;
});

EM_JS(double, cp5_pixel_ratio, (), {
    try { if (window.localStorage.getItem("no_retina")) return 1; } catch (e) {}
    return window.devicePixelRatio || 1;
});

EM_JS(void, cp5_select_layer, (int index), {
    var M = Module.cp5, layer = M.layers[index];
    M.activeLayer = index;
    M.ctxs[0] = layer.ctx;
    M.st[0] = layer.st;
});

EM_JS(void, cp5_resize_layers, (int width, int height), {
    var M = Module.cp5, layer = M.layers[1], canvas = layer.ctx.canvas;
    M.layers[0].st = M.mkst();
    if (canvas.width !== width || canvas.height !== height) {
        canvas.width = width;
        canvas.height = height;
        layer.st = M.mkst();
    }
    M.st[0] = M.layers[M.activeLayer].st;
});

EM_JS(int, cp5_create, (), {
    var ctx = document.createElement("canvas").getContext("2d");
    var M = Module.cp5, a = M.ctxs;
    for (var i = 1; i < a.length; ++i) if (a[i] == null) { a[i] = ctx; M.st[i] = M.mkst(); return i; }
    a.push(ctx);
    M.st.push(M.mkst());
    return a.length - 1;
});

EM_JS(void, cp5_destroy, (int id), { Module.cp5.ctxs[id] = null; Module.cp5.st[id] = null; });

EM_JS(void, cp5_flush, (double* cmds, int n, const char* strs), {
    var M = Module.cp5, ctxs = M.ctxs, st = M.st;
    var hex = M.hex, font = M.font, baseline = M.baseline, join = M.join, cap = M.cap;
    var d = HEAPF64, p = cmds >> 3, end = p + n;
    function A(c, s, v) { if (s.a !== v) { s.a = v; c.globalAlpha = v; } }
    function F(c, s, v) { if (s.f !== v) { s.f = v; c.fillStyle = hex(v); } }
    function S(c, s, v) { if (s.s !== v) { s.s = v; c.strokeStyle = hex(v); } }
    function LW(c, s, v) { if (s.lw !== v) { s.lw = v; c.lineWidth = v; } }
    function LJ(c, s, v) { if (s.lj !== v) { s.lj = v; c.lineJoin = join[v]; } }
    function LC(c, s, v) { if (s.lc !== v) { s.lc = v; c.lineCap = cap[v]; } }
    function ML(c, s, v) { if (s.ml !== v) { s.ml = v; c.miterLimit = v; } }
    function FO(c, s, v) { if (s.fo !== v) { s.fo = v; c.font = font(v); } }
    function TB(c, s, v) { if (s.tb !== v) { s.tb = v; c.textBaseline = baseline[v]; } }
    while (p < end) {
        var op = d[p++];
        var id = d[p++];
        var c = ctxs[id];
        var stk = st[id];
        var s = stk[stk.length - 1];
        switch (op) {
        case 0: c.save(); stk.push({f:s.f, s:s.s, a:s.a, lw:s.lw, lj:s.lj, lc:s.lc, ml:s.ml, fo:s.fo, tb:s.tb}); break;
        case 1: c.restore(); if (stk.length > 1) stk.pop(); break;
        case 2: c.setTransform(d[p++], d[p++], d[p++], d[p++], d[p++], d[p++]); break;
        case 3: c.translate(d[p++], d[p++]); break;
        case 4: c.rotate(d[p++]); break;
        case 5: c.scale(d[p++], d[p++]); break;
        case 6: c.beginPath(); break;
        case 7: c.closePath(); break;
        case 8: c.moveTo(d[p++], d[p++]); break;
        case 9: c.lineTo(d[p++], d[p++]); break;
        case 10: c.arc(d[p++], d[p++], d[p++], d[p++], d[p++], !!d[p++]); break;
        case 11: c.rect(d[p++], d[p++], d[p++], d[p++]); break;
        case 12: F(c, s, d[p++]); A(c, s, d[p++]); c.fill(); break;
        case 13: S(c, s, d[p++]); A(c, s, d[p++]); LW(c, s, d[p++]); LJ(c, s, d[p++]); LC(c, s, d[p++]); ML(c, s, d[p++]); c.stroke(); break;
        case 14: { var x = d[p++], y = d[p++], w = d[p++], h = d[p++]; F(c, s, d[p++]); A(c, s, d[p++]); c.fillRect(x, y, w, h); break; }
        case 15: { var x = d[p++], y = d[p++], w = d[p++], h = d[p++]; S(c, s, d[p++]); A(c, s, d[p++]); LW(c, s, d[p++]); LJ(c, s, d[p++]); LC(c, s, d[p++]); ML(c, s, d[p++]); c.strokeRect(x, y, w, h); break; }
        case 16: c.setTransform(1, 0, 0, 1, 0, 0); c.clearRect(0, 0, d[p++], d[p++]); break;
        case 17: c.save(); c.setTransform(1, 0, 0, 1, 0, 0); c.clearRect(d[p++], d[p++], d[p++], d[p++]); c.restore(); break;
        case 18: { var cv = c.canvas, w = d[p++], h = d[p++], rs = false;
                   if (cv.width !== w) { cv.width = w; rs = true; }
                   if (cv.height !== h) { cv.height = h; rs = true; }
                   if (rs) st[id] = M.mkst();
                   break; }
        case 19: { var src = d[p++], dx = d[p++], dy = d[p++]; A(c, s, d[p++]); c.drawImage(ctxs[src].canvas, dx, dy); break; }
        case 20: { var src = d[p++], sx = d[p++], sy = d[p++], sw = d[p++], sh = d[p++],
                       dx = d[p++], dy = d[p++], dw = d[p++], dh = d[p++];
                   A(c, s, d[p++]); c.drawImage(ctxs[src].canvas, sx, sy, sw, sh, dx, dy, dw, dh); break; }
        case 21: { var t = UTF8ToString(strs + d[p++], d[p++]), x = d[p++], y = d[p++];
                   F(c, s, d[p++]); A(c, s, d[p++]); FO(c, s, d[p++]); TB(c, s, d[p++]);
                   c.fillText(t, x, y); break; }
        case 22: { var t = UTF8ToString(strs + d[p++], d[p++]), x = d[p++], y = d[p++];
                   S(c, s, d[p++]); A(c, s, d[p++]); FO(c, s, d[p++]); TB(c, s, d[p++]); LW(c, s, d[p++]); LJ(c, s, 0);
                   c.strokeText(t, x, y); break; }
        case 23: { var t = UTF8ToString(strs + d[p++], d[p++]), x = d[p++], y = d[p++];
                   var fr = d[p++], sr = d[p++];
                   A(c, s, d[p++]); FO(c, s, d[p++]); TB(c, s, d[p++]); LW(c, s, d[p++]); LJ(c, s, 0);
                   S(c, s, sr); c.strokeText(t, x, y);
                   F(c, s, fr); c.fillText(t, x, y); break; }
        case 24: c.clip(); break;
        case 25: { var src = d[p++], x = d[p++], y = d[p++], w = d[p++], h = d[p++]; A(c, s, d[p++]);
                   s.f = -1; c.fillStyle = c.createPattern(ctxs[src].canvas, null); c.fillRect(x, y, w, h); break; }
        }
    }
});

namespace cp5 {

namespace detail {
    std::vector<double> cmdBuf;
    std::vector<char> strBuf;
}
using namespace detail;

static int gCanvasW = 1, gCanvasH = 1;
static int gCanvasResizes = 0;
static double gPixelRatio = 1;
int canvasResizeCount() { return gCanvasResizes; }
static int gContexts = 0;
int contextCount() { return gContexts; }

void flush() {
    if (cmdBuf.empty()) return;
    static const char empty = 0;
    cp5_flush(cmdBuf.data(), (int)cmdBuf.size(), strBuf.empty() ? &empty : strBuf.data());
    cmdBuf.clear();
    strBuf.clear();
}

bool backendInit(const char* canvasSelector) {
    cmdBuf.reserve(1 << 16);
    strBuf.reserve(1 << 12);
    gPixelRatio = cp5_pixel_ratio();
    return cp5_boot(canvasSelector) != 0;
}

void backendBeginFrame() {
    flush();
    textFrame();
    cp5_select_layer(0);
    double cssW, cssH;
    emscripten_get_element_css_size("#canvas", &cssW, &cssH);
    int w = (int)(cssW * gPixelRatio), h = (int)(cssH * gPixelRatio);
    if (w != gCanvasW || h != gCanvasH) {
        gCanvasW = w > 0 ? w : 1;
        gCanvasH = h > 0 ? h : 1;
        emscripten_set_canvas_element_size("#canvas", gCanvasW, gCanvasH);
        cp5_resize_layers(gCanvasW, gCanvasH);
        ++gCanvasResizes;
    }
}

void useOverlay() {
    flush();
    cp5_select_layer(1);
}

double pixelRatio() { return gPixelRatio; }
int canvasWidth() { return gCanvasW; }
int canvasHeight() { return gCanvasH; }

Context::Context(bool offscreen) {
    ++gContexts;
    target = offscreen ? cp5_create() : 0;
    if (!offscreen) {
        w = gCanvasW;
        h = gCanvasH;
    }
}

Context::~Context() {
    --gContexts;
    if (target > 0) { flush(); cp5_destroy(target); }
}

void Context::setSize(int width, int height) {
    // Resizing resets the canvas, transform included.
    if (target > 0 && (width != w || height != h)) {
        std::copy_n(State().m, 6, state.m);
        damage.clear();
    }
    w = width;
    h = height;
    if (target > 0) { cmd(opSetSize, target); arg(width); arg(height); }
}

void Context::clear() {
    if (target == 0) {
        w = gCanvasW;
        h = gCanvasH;
    }
    std::copy_n(State().m, 6, state.m);
    cmd(opClear, target); arg(w); arg(h);
}

void Context::clearRegion(double x, double y, double ww, double hh) {
    cmd(opClearRegion, target); arg(x); arg(y); arg(ww); arg(hh);
}

void Context::save() {
    saveStack.push_back(state);
    cmd(opSave, target);
}

void Context::restore() {
    if (saveStack.empty()) return;
    state = saveStack.back();
    saveStack.pop_back();
    cmd(opRestore, target);
}

void Context::setTransform(double a, double b, double c, double d, double e, double f) {
    double* m = state.m;
    m[0] = a; m[1] = b; m[2] = c; m[3] = d; m[4] = e; m[5] = f;
    cmd(opTransform, target); arg(a); arg(b); arg(c); arg(d); arg(e); arg(f);
}
void Context::translate(double x, double y) {
    double* m = state.m;
    m[4] += m[0] * x + m[2] * y;
    m[5] += m[1] * x + m[3] * y;
    cmd(opTranslate, target); arg(x); arg(y);
}
void Context::rotate(double angle) {
    double* m = state.m;
    double c = std::cos(angle), s = std::sin(angle);
    double a = m[0], b = m[1];
    m[0] = a * c + m[2] * s;
    m[1] = b * c + m[3] * s;
    m[2] = m[2] * c - a * s;
    m[3] = m[3] * c - b * s;
    cmd(opRotate, target); arg(angle);
}
void Context::scale(double x, double y) {
    double* m = state.m;
    m[0] *= x; m[1] *= x;
    m[2] *= y; m[3] *= y;
    cmd(opScale, target); arg(x); arg(y);
}

void Context::beginPath() {
    pathEmpty = true;
    cmd(opBegin, target);
}
void Context::closePath() { cmd(opClose, target); }
void Context::moveTo(double x, double y) {
    if (tracking) extendPath(x, y);
    cmd(opMove, target); arg(x); arg(y);
}
void Context::lineTo(double x, double y) {
    if (tracking) extendPath(x, y);
    cmd(opLine, target); arg(x); arg(y);
}
void Context::arc(double x, double y, double radius, double startAngle, double endAngle, bool counterclockwise) {
    if (tracking) {
        extendPath(x - radius, y - radius); extendPath(x + radius, y - radius);
        extendPath(x - radius, y + radius); extendPath(x + radius, y + radius);
    }
    cmd(opArc, target); arg(x); arg(y); arg(radius); arg(startAngle); arg(endAngle); arg(counterclockwise ? 1 : 0);
}
void Context::rect(double x, double y, double width, double height) {
    if (tracking) {
        extendPath(x, y); extendPath(x + width, y);
        extendPath(x, y + height); extendPath(x + width, y + height);
    }
    cmd(opRect, target); arg(x); arg(y); arg(width); arg(height);
}

void Context::fill() {
    if (tracking && !pathEmpty) addDamage(pathBox);
    cmd(opFill, target); arg(state.fill); arg(state.alpha);
}
void Context::clip() { cmd(opClip, target); }
void Context::stroke() {
    if (tracking && !pathEmpty) {
        double r = strokeReach();
        addDamage({pathBox.x0 - r, pathBox.y0 - r, pathBox.x1 + r, pathBox.y1 + r});
    }
    cmd(opStroke, target); arg(state.stroke); arg(state.alpha); arg(state.lineWidth);
    arg((int)state.lineJoin); arg((int)state.lineCap); arg(state.miterLimit);
}
void Context::fillRect(double x, double y, double width, double height) {
    if (tracking) addLocal(x, y, x + width, y + height, 0);
    cmd(opFillRect, target); arg(x); arg(y); arg(width); arg(height); arg(state.fill); arg(state.alpha);
}
void Context::strokeRect(double x, double y, double width, double height) {
    if (tracking) addLocal(x, y, x + width, y + height, strokeReach());
    cmd(opStrokeRect, target); arg(x); arg(y); arg(width); arg(height); arg(state.stroke); arg(state.alpha);
    arg(state.lineWidth); arg((int)state.lineJoin); arg((int)state.lineCap); arg(state.miterLimit);
}

void Context::fillPattern(Context& src, double x, double y, double width, double height) {
    if (src.target <= 0 || src.w <= 0 || src.h <= 0) return;
    if (tracking) addLocal(x, y, x + width, y + height, 0);
    cmd(opFillPattern, target); arg(src.target); arg(x); arg(y); arg(width); arg(height); arg(state.alpha);
}

void Context::drawContext(Context& src, double dx, double dy) {
    if (src.target <= 0 || src.w <= 0 || src.h <= 0) return;
    if (tracking) addLocal(dx, dy, dx + src.w, dy + src.h, 0);
    cmd(opDraw, target); arg(src.target); arg(dx); arg(dy); arg(state.alpha);
}

void Context::drawContext(Context& src, double sx, double sy, double sw, double sh,
                          double dx, double dy, double dw, double dh) {
    if (src.target <= 0 || src.w <= 0 || src.h <= 0) return;
    if (tracking) addLocal(dx, dy, dx + dw, dy + dh, 0);
    cmd(opDrawSub, target); arg(src.target); arg(sx); arg(sy); arg(sw); arg(sh);
    arg(dx); arg(dy); arg(dw); arg(dh); arg(state.alpha);
}

// ---- Layer bounds ----

namespace {
    // Antialiasing and filtering reach a pixel past the geometry; the rest is slack.
    constexpr double DAMAGE_PAD = 2;
    // Regions closer than this merge, trading a little area for fewer composites.
    constexpr double DAMAGE_MERGE_GAP = 32;
    constexpr size_t DAMAGE_MAX_REGIONS = 12;

    double area(double x0, double y0, double x1, double y1) { return (x1 - x0) * (y1 - y0); }
}

void Context::extendPath(double x, double y) {
    const double* m = state.m;
    double px = m[0] * x + m[2] * y + m[4], py = m[1] * x + m[3] * y + m[5];
    if (pathEmpty) {
        pathBox = {px, py, px, py};
        pathEmpty = false;
        return;
    }
    pathBox.x0 = std::min(pathBox.x0, px); pathBox.x1 = std::max(pathBox.x1, px);
    pathBox.y0 = std::min(pathBox.y0, py); pathBox.y1 = std::max(pathBox.y1, py);
}

double Context::strokeReach() const {
    const double* m = state.m;
    // The Frobenius norm bounds how far the transform stretches any direction.
    double stretch = std::sqrt(m[0] * m[0] + m[1] * m[1] + m[2] * m[2] + m[3] * m[3]);
    double joint = state.lineJoin == LineJoin::miter ? std::max(state.miterLimit, 1.5) : 1.5;
    return state.lineWidth * 0.5 * joint * stretch;
}

void Context::addLocal(double x0, double y0, double x1, double y1, double pad) {
    const double* m = state.m;
    double xs[4] = {x0, x1, x0, x1}, ys[4] = {y0, y0, y1, y1};
    Box b{1e300, 1e300, -1e300, -1e300};
    for (int i = 0; i < 4; ++i) {
        double px = m[0] * xs[i] + m[2] * ys[i] + m[4], py = m[1] * xs[i] + m[3] * ys[i] + m[5];
        b.x0 = std::min(b.x0, px); b.x1 = std::max(b.x1, px);
        b.y0 = std::min(b.y0, py); b.y1 = std::max(b.y1, py);
    }
    addDamage({b.x0 - pad, b.y0 - pad, b.x1 + pad, b.y1 + pad});
}

void Context::addDamage(Box b) {
    b.x0 = std::max(0.0, std::floor(b.x0 - DAMAGE_PAD));
    b.y0 = std::max(0.0, std::floor(b.y0 - DAMAGE_PAD));
    b.x1 = std::min((double)w, std::ceil(b.x1 + DAMAGE_PAD));
    b.y1 = std::min((double)h, std::ceil(b.y1 + DAMAGE_PAD));
    if (!(b.x1 > b.x0 && b.y1 > b.y0)) return;
    for (;;) {
        // Absorb every region this one comes near, so the regions stay disjoint.
        bool merged = false;
        for (size_t i = 0; i < damage.size(); ++i) {
            const Box& o = damage[i];
            if (b.x0 > o.x1 + DAMAGE_MERGE_GAP || o.x0 > b.x1 + DAMAGE_MERGE_GAP ||
                b.y0 > o.y1 + DAMAGE_MERGE_GAP || o.y0 > b.y1 + DAMAGE_MERGE_GAP) continue;
            b = {std::min(b.x0, o.x0), std::min(b.y0, o.y0), std::max(b.x1, o.x1), std::max(b.y1, o.y1)};
            damage[i] = damage.back();
            damage.pop_back();
            merged = true;
            break;
        }
        if (merged) continue;
        if (damage.size() < DAMAGE_MAX_REGIONS) break;
        // Too many: fold in whichever region grows the union least.
        size_t best = 0;
        double bestGrowth = 1e300;
        for (size_t i = 0; i < damage.size(); ++i) {
            const Box& o = damage[i];
            double grown = area(std::min(b.x0, o.x0), std::min(b.y0, o.y0), std::max(b.x1, o.x1), std::max(b.y1, o.y1)) -
                           area(b.x0, b.y0, b.x1, b.y1) - area(o.x0, o.y0, o.x1, o.y1);
            if (grown < bestGrowth) { bestGrowth = grown; best = i; }
        }
        const Box o = damage[best];
        b = {std::min(b.x0, o.x0), std::min(b.y0, o.y0), std::max(b.x1, o.x1), std::max(b.y1, o.y1)};
        damage[best] = damage.back();
        damage.pop_back();
    }
    damage.push_back(b);
}

void Context::beginLayer(int width, int height) {
    if (width != w || height != h) {
        setSize(width, height);
    } else if (!tracking) {
        clear();
    } else {
        double drawn = 0;
        for (const Box& b : damage) drawn += area(b.x0, b.y0, b.x1, b.y1);
        // A whole-canvas clear is the cheap path once most of it is dirty anyway.
        if (drawn > 0.5 * w * h) clear();
        else for (const Box& b : damage) clearRegion(b.x0, b.y0, b.x1 - b.x0, b.y1 - b.y0);
    }
    damage.clear();
    tracking = true;
    resetTransform();
    setAlpha(1);
}

void Context::compositeLayer(Context& dst) {
    for (const Box& b : damage) {
        double bw = b.x1 - b.x0, bh = b.y1 - b.y0;
        dst.drawContext(*this, b.x0, b.y0, bw, bh, b.x0, b.y0, bw, bh);
    }
}

} // namespace cp5
