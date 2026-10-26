#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace cp5 {

enum class LineJoin { round, bevel, miter };
enum class LineCap { butt, round, square };
enum class Baseline { top, hanging, middle, alphabetic, ideographic, bottom };

bool backendInit(const char* canvasSelector);
void backendBeginFrame();
int canvasWidth();
int canvasHeight();
int canvasResizeCount();
// Canvas pixels per CSS pixel, fixed at boot: 1 while localStorage "no_retina" is set,
// else devicePixelRatio.
double pixelRatio();
// Live Context objects (main canvas included).
int contextCount();
// Executes all buffered draw commands in one JS call.
void flush();
void useOverlay();

class Context {
public:
    explicit Context(bool offscreen);
    ~Context();
    Context(const Context&) = delete;
    Context& operator=(const Context&) = delete;

    void setSize(int width, int height);
    int width() const { return w; }
    int height() const { return h; }
    // The JS-side id of an offscreen context's canvas.
    int handle() const { return target; }
    void clear();
    void clearRegion(double x, double y, double w, double h);

    void save();
    void restore();

    void setAlpha(double a) { state.alpha = a; }
    void scaleAlpha(double a) { state.alpha *= a; }
    double alpha() const { return state.alpha; }
    void setLineWidth(double width) { state.lineWidth = width; }
    void setLineJoin(LineJoin j) { state.lineJoin = j; }
    void setLineCap(LineCap c) { state.lineCap = c; }
    void setFontSize(double size) { state.fontSize = size; }
    void setTextBaseline(Baseline b) { state.baseline = b; }
    void setFillColor(uint32_t rgb) { state.fill = rgb & 0xffffff; }
    void setStrokeColor(uint32_t rgb) { state.stroke = rgb & 0xffffff; }

    void setTransform(double a, double b, double c, double d, double e, double f);
    void resetTransform() { setTransform(1, 0, 0, 1, 0, 0); }
    void translate(double x, double y);
    void rotate(double angle);
    void scale(double x, double y);

    void beginPath();
    void closePath();
    void moveTo(double x, double y);
    void lineTo(double x, double y);
    void arc(double x, double y, double radius, double startAngle, double endAngle, bool counterclockwise = false);
    void rect(double x, double y, double width, double height);

    void fill();
    // Intersects the clip region with the current path.
    void clip();
    void stroke();
    void fillRect(double x, double y, double width, double height);
    void strokeRect(double x, double y, double width, double height);
    // Fills the rect with src's canvas repeated (createPattern), tiled from the origin.
    void fillPattern(Context& src, double x, double y, double width, double height);

    void fillText(const std::string& text, double x, double y);
    void strokeText(const std::string& text, double x, double y);
    void fillStrokeText(const std::string& text, double x, double y);
    double measureText(const std::string& text);
    void labelSize(const std::string& text, double* w, double* h);

    void drawContext(Context& src, double dx, double dy);
    void drawContext(Context& src, double sx, double sy, double sw, double sh, double dx, double dy, double dw, double dh);

    // An offscreen layer redrawn and composited every frame. beginLayer() sizes it, clears what
    // the previous frame drew (all of it after a resize), resets the transform and alpha, and
    // starts collecting the device-pixel bounds of each draw. compositeLayer() draws just those
    // regions onto dst at dst's alpha; dst's transform must be the identity. The regions are
    // disjoint and cover every drawn pixel, so the result is that of drawing the whole layer.
    void beginLayer(int width, int height);
    void compositeLayer(Context& dst);

private:
    struct Box { double x0, y0, x1, y1; };
    struct State {
        double alpha = 1;
        double lineWidth = 1;
        double miterLimit = 10;
        double fontSize = 10;
        uint32_t fill = 0;
        uint32_t stroke = 0;
        LineJoin lineJoin = LineJoin::miter;
        LineCap lineCap = LineCap::butt;
        Baseline baseline = Baseline::alphabetic;
        // The canvas transform (a, b, c, d, e, f), mirrored for the layer bounds.
        double m[6] = {1, 0, 0, 1, 0, 0};
    };

    State state;
    std::vector<State> saveStack;
    int target;
    int w = 0, h = 0;

    bool tracking = false;
    std::vector<Box> damage;
    Box pathBox{};
    bool pathEmpty = true;
    void extendPath(double x, double y);
    // Adds the local rect, transformed, grown by pad device pixels.
    void addLocal(double x0, double y0, double x1, double y1, double pad);
    void addDamage(Box b);
    void addText(const std::string& text, double x, double y, bool stroked);
    // How far a stroke reaches past its path, in device pixels.
    double strokeReach() const;
};

} // namespace cp5
