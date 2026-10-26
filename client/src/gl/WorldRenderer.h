#pragma once

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace cp5 { class Context; }

// WebGL2 world renderer: every shape is one instanced quad whose fragment shader evaluates the
// exact signed distance to its outline, so fill, round-joined stroke and their antialiasing
// match the Canvas2D path (fill, then stroke over it, both at the shape's alpha). Alpha groups
// render into an offscreen layer composited over the rect GameRendering gives.
class WorldRenderer {
public:
    // False while WebGL2 is unavailable or the context is lost: the caller draws the world itself.
    bool beginFrame(int width, int height, double cameraX, double cameraY, double zoom);
    // What follows until endComposite fades as one at alpha; x/y/w/h bound it in device pixels.
    void beginComposite(double alpha, double x, double y, double w, double h);
    void endComposite();
    void shape(int sides, uint32_t physicsFlags, uint32_t styleFlags, double size, double width,
               double borderWidth, double x, double y, double angle, uint32_t fill, uint32_t stroke,
               double alpha);
    // The background, drawn before anything else in the frame. While a frame has one, the
    // base canvas under this one is hidden.
    void solidBackground(uint32_t rgb);
    // The tile's canvas (uploaded again when changed) repeated like a Canvas2D pattern fill
    // under setTransform(scale, 0, 0, scale, 0, 0) and translate(originX, originY), sampled
    // bilinearly.
    void patternBackground(cp5::Context& tile, bool changed, double scale, double originX, double originY);
    // A Canvas2D fillRect in device pixels.
    void screenRect(double x, double y, double w, double h, uint32_t rgb, double alpha);
    // A cp5 canvas (uploaded again when changed) over the world-space quad with corners
    // top-left, top-right, bottom-left, bottom-right, sampled bilinearly.
    void image(cp5::Context& canvas, bool changed, const double corners[8], double alpha);
    void endFrame();
    void disable();

private:
    struct Instance {
        float x, y;
        float size, width, borderWidth, angle;
        float kind, count, param, alpha;
        uint32_t fill, stroke;
    };
    struct Layer {
        unsigned fbo = 0, texture = 0;
        int width = 0, height = 0;
    };
    struct Rect { int x0, y0, x1, y1; };
    struct Composite { float alpha; Rect rect; };

    int context = 0;
    bool failed = false;
    bool ready = false;
    bool active = false;
    bool shown = false;
    bool coversBase = false, baseCovered = false;
    int width = 0, height = 0;
    float cameraX = 0, cameraY = 0, zoom = 1;

    unsigned shapeProgram = 0, blitProgram = 0, patternProgram = 0, imageProgram = 0;
    unsigned vao = 0, blitVao = 0, instanceBuffer = 0;
    int uCamera = -1, uZoom = -1, uScreen = -1, uBlitAlpha = -1, uBlitTexture = -1;
    int uPatternMap = -1, uPatternHeight = -1, uPatternTileSize = -1, uPatternTexture = -1;
    int uImageCorners = -1, uImageCamera = -1, uImageZoom = -1, uImageScreen = -1, uImageAlpha = -1,
        uImageTexture = -1;
    std::vector<Instance> instances;
    std::vector<Layer> layers;
    std::vector<Composite> composites;
    // GL textures of cp5 canvases, by canvas id.
    std::unordered_map<int, unsigned> canvasTextures;

    bool init();
    void setShown(bool value);
    void setBaseCovered(bool value);
    // Binds canvas's texture to unit 0, uploading it first when new or changed.
    void bindCanvas(int canvas, bool changed, bool repeat);
    void flush();
    void bindTarget(size_t depth);
    void scissorTo(const Rect& r);
};
