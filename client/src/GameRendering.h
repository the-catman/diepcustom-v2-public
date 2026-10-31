#pragma once

#include <memory>
#include "sim/QuadTree.h"
#include <string>
#include <unordered_map>
#include <vector>

#include "cp5/Context.h"
#include "gl/WorldRenderer.h"
#include "sim/Simulation.h"

class GameRendering {
public:
    void render(cp5::Context& ctx, Simulation& sim, double now);

    // Set on the connection's first spawn, cleared on disconnect. The grid fades in toward
    // it and entities only draw while it's set.
    bool spawnedThisConnection = false;

    float zoom() const { return solvedZoom; }
    float cameraX() const { return camX; }
    float cameraY() const { return camY; }
    float dt() const { return frameTime; }
    // 1000 / the mean of the last 60 frame times; -1 until 60 frames have passed.
    double fps() const { return fpsAverage; }

    // UI units: u = max(w / 1920, h / 1080) and n units are float(ui_scale * float(u * n)).
    static float uiBase();
    static float uiUnits(float n);

    void drawBar(cp5::Context& ctx, double perc, double cx, double cy, double w, double h, uint32_t fill);
    void drawLabel(cp5::Context& ctx, const std::string& text, double cx, double cy, double size, uint32_t fill,
                   bool stroke = true, uint32_t strokeColor = 0x000000, double strokeRatio = 0.2);
    // UI text goes into an int w x h box: w = int(width + 2 * margin), h = int(1.4 * size),
    // margin = int(2 * strokeRatio * size), glyphs at (margin, h / 2) on a middle baseline.
    // Callers put the box corner on whole pixels.
    struct TextBox { int w = 1, h = 1, margin = 0; };
    TextBox textBox(cp5::Context& ctx, const std::string& text, double size, double strokeRatio = 0.2);
    void drawTextBox(cp5::Context& ctx, const std::string& text, const TextBox& box, double left, double top,
                     double size, uint32_t fill, bool stroke = true, uint32_t strokeColor = 0x000000,
                     double strokeRatio = 0.2);
    // A tank preview rendered once into its own canvas and blitted from then on. It redraws
    // when the tank, scale or preview look changes.
    struct TankIcon {
        std::unique_ptr<cp5::Context> canvas;
        int tankId = -1;
        double scale = 0;
        int look = -1;
        // The drawn part, from the canvas' top left.
        int w = 0, h = 0;
        // The AABB center from the tank origin, in pixels.
        double centerX = 0, centerY = 0;
    };
    // With render false a stale icon is skipped instead of redrawn.
    void drawTankIcon(cp5::Context& ctx, TankIcon& icon, int tankId, double cx, double cy, double angle, double scale,
                      bool centerAroundAABB = false, bool render = true);
    void renderHUD(cp5::Context& ctx, Simulation& sim, double now, double mouseX, double mouseY,
                   int playersOnline, const std::string& pingText, bool hudVisible);
    void onNewConnection();

private:
    double lastNow = -1;
    float frameTime = 16.7f;
    float frameTimes[60] = {};
    int frameIndex = 0, frameCount = 0;
    double fpsAverage = -1;

    bool firstSolve = true;
    float camX = 0, camY = 0;
    float fovZoom = 1, solvedZoom = 1;
    float gridFade = 0;
    bool leaderInit = false;
    float leaderSmoothX = 0, leaderSmoothY = 0;
    std::unordered_map<uint32_t, std::pair<float, float>> mothershipSmooth;

    bool bordersInit = false;
    float borderMinX = 0, borderMinY = 0, borderMaxX = 0, borderMaxY = 0;

    std::unique_ptr<cp5::Context> minimapContent;
    double minimapContentTime = -1e18;

    float levelBarPerc = 0;
    float levelCreep = 0;
    bool levelBarDrain = false;
    std::string levelText;
    float scoreBarPerc = 0;
    float scoreboardVals[10] = {};
    float scoreboardHover[10] = {};
    TankIcon scoreboardIcons[10];

    // Everything a preview's pixels depend on besides the tank and scale. previewLookId
    // counts its changes; icons drawn under an older one redraw.
    struct PreviewLook {
        uint32_t border = 0, barrel = 0, solidStroke = 0;
        double strokeIntensity = 0;
        bool softStroke = false, debugCollisions = false;
        int tankDefs = -1;
        bool operator==(const PreviewLook& o) const {
            return border == o.border && barrel == o.barrel && solidStroke == o.solidStroke &&
                   strokeIntensity == o.strokeIntensity && softStroke == o.softStroke &&
                   debugCollisions == o.debugCollisions && tankDefs == o.tankDefs;
        }
    };
    PreviewLook previewLook;
    int previewLookId = 0;
    void updatePreviewLook();
    void renderTankIcon(TankIcon& icon, int tankId, double scale);
    double screenRectX = 0, screenRectY = 0, screenRectW = 0, screenRectH = 0;

    std::vector<std::unique_ptr<cp5::Context>> scratchPool;
    std::unique_ptr<cp5::Context> gridTile;
    // What the tile was last drawn with.
    double tileAlpha = -1, tileLineWidth = -1;
    uint32_t tileBackground = 0, tileGridColor = 0;
    // GL arrow labels: drawLabel's text rastered at scale device pixels per world unit, its
    // origin margin pixels in from the left.
    struct ArrowLabel {
        std::unique_ptr<cp5::Context> canvas;
        double scale = 0, width = 0;
        int margin = 0;
    };
    std::unordered_map<std::string, ArrowLabel> arrowLabels;
    QuadTree debugTree;
    size_t scratchDepth = 0;
    WorldRenderer worldRenderer;

    void solveCamera(Simulation& sim, double now);
    // With gl, the grid, borders and arrows draw on the GL world canvas instead.
    void renderGrid(cp5::Context& ctx, WorldRenderer* gl = nullptr);
    // Redraws the pattern tile when its look changed; true when it did.
    bool drawGridTile(double alpha);
    void renderBorders(cp5::Context& ctx, Simulation& sim, WorldRenderer* gl = nullptr);
    struct Frame {
        double x = 0, y = 0, angle = 0;
        // Damage flash and invuln blink come from the root, so parts flash with the body.
        double rootDamageTime = -1e18;
        bool rootFlashing = false;
        double rootFlagsTime = -1e18;
        bool present = false;
    };
    // Scene graph cached across frames, rebuilt when Simulation::sceneDirty.
    struct ChildLists { std::vector<Entity*> before, after; };
    using ChildMap = std::unordered_map<const Entity*, ChildLists>;
    std::vector<Entity*> sceneRoots;
    ChildMap sceneChildren;
    void rebuildScene(Simulation& sim);

    void renderEntities(cp5::Context& ctx, Simulation& sim, double now, WorldRenderer* gl = nullptr);
    void renderEntity(cp5::Context& ctx, Simulation& sim, Entity& e, double now, bool applyAlpha, double strokeScale, Frame parent,
                      const ChildMap& childMap, bool allowComposite = true, WorldRenderer* gl = nullptr);
    // Max extent of an entity plus its descendants (barrels reach past the body).
    double subtreeReach(Entity& e, double now, const ChildMap& childMap);
    void renderShape(cp5::Context& ctx, Entity& e, double now, double strokeScale, double x, double y, double angle,
                     const Frame& root, WorldRenderer* gl = nullptr, double alpha = 1);
    void renderNames(cp5::Context& ctx, Simulation& sim, double now);
    void renderDebugCollisions(cp5::Context& ctx, double now);
    void renderDebugCollisions(cp5::Context& ctx, Entity& e, double now, const Frame& parent);
    void subtreeBounds(Entity& e, double now, const Frame& f, double box[4]);
    void updateDebugTree(Simulation& sim, double now);
    void placeInDebugTree(Entity& e, double now, const Frame& parent);
    void renderDebugTree(cp5::Context& ctx, const QuadTree::Node& node);
    Frame frameOf(Entity& e, double now, const Frame& parent);
    // frameOf through the entity's parents.
    Frame worldFrame(Simulation& sim, Entity& e, double now);
    void renderHealthBars(cp5::Context& ctx, Simulation& sim, double now);
    void renderArrow(cp5::Context& ctx, const std::string& text, double tx, double ty, uint32_t color,
                     WorldRenderer* gl = nullptr);
    void renderLeaderArrow(cp5::Context& ctx, Simulation& sim, double now, WorldRenderer* gl = nullptr);
    void renderMothershipArrows(cp5::Context& ctx, Simulation& sim, double now, WorldRenderer* gl = nullptr);
};
