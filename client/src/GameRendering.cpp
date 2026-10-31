#include "GameRendering.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include "Const.h"
#include "TankDefs.h"
#include <cstdio>
#include <cstdlib>
#include <emscripten/emscripten.h>

namespace {
    constexpr double GRID_SIZE = 50;

    float expSmooth(float current, float target, float rate, float dtMs) {
        float k = 1.0f - std::pow(1.0f - rate, dtMs * 60.0f / 1000.0f);
        return current + (target - current) * k;
    }

    struct RGB {
        float r, g, b;
        uint32_t pack() const {
            auto c = [](float v) { return (uint32_t)std::min(255.0f, std::max(0.0f, v)); };
            return (c(r) << 16) | (c(g) << 8) | c(b);
        }
    };
    RGB unpack(uint32_t rgb) { return {(float)((rgb >> 16) & 0xff), (float)((rgb >> 8) & 0xff), (float)(rgb & 0xff)}; }
    RGB mix(RGB a, RGB b, float t) { return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t}; }
}

void GameRendering::render(cp5::Context& ctx, Simulation& sim, double now) {
    frameTime = lastNow < 0 ? 16.7f : (float)std::min(1000.0, now - lastNow);
    lastNow = now;
    frameTimes[frameIndex] = frameTime;
    frameIndex = (frameIndex + 1) % 60;
    frameCount = std::min(frameCount + 1, 60);
    if (frameCount == 60) {
        double sum = 0;
        for (float t : frameTimes) sum += t;
        fpsAverage = 1000.0 / (sum / 60.0);
    }

    updatePreviewLook();
    solveCamera(sim, now);

    bool useGL = config::renWorldGL && worldRenderer.beginFrame(
        cp5::canvasWidth(), cp5::canvasHeight(), camX, camY, solvedZoom);
    if (!config::renWorldGL) worldRenderer.disable();
    // The GL canvas also takes the background, borders and arrows and hides the base canvas,
    // unless the debug quadtree (drawn between them and the entities) or the line grid needs it.
    WorldRenderer* glBase = useGL && config::renBackground && (config::renSolidBackground || config::renPatternGrid) &&
                                    !config::renDebugCollisions
                                ? &worldRenderer
                                : nullptr;

    if (config::renBackground) renderGrid(ctx, glBase);
    renderBorders(ctx, sim, glBase);

    screenRectW = cp5::canvasWidth() / (double)solvedZoom;
    screenRectH = cp5::canvasHeight() / (double)solvedZoom;
    screenRectX = camX - screenRectW / 2;
    screenRectY = camY - screenRectH / 2;

    ctx.save();
    ctx.translate(cp5::canvasWidth() / 2.0, cp5::canvasHeight() / 2.0);
    ctx.scale(solvedZoom, solvedZoom);
    ctx.translate(-camX, -camY);
    if (config::renDebugCollisions) {
        // Blue quadtree leaves, under the entities, in screen space.
        updateDebugTree(sim, now);
        ctx.save();
        ctx.resetTransform();
        ctx.setStrokeColor(0x0000ff);
        ctx.setLineWidth(1);
        renderDebugTree(ctx, debugTree.root());
        ctx.restore();
    }
    // Before the connection's first spawn only the background and borders draw.
    if (spawnedThisConnection) {
        renderLeaderArrow(ctx, sim, now, glBase);
        renderMothershipArrows(ctx, sim, now, glBase);
        renderEntities(ctx, sim, now, useGL ? &worldRenderer : nullptr);
    }
    if (useGL) {
        worldRenderer.endFrame();
        ctx.restore();
        cp5::useOverlay();
        ctx.clear();
        ctx.save();
        ctx.translate(cp5::canvasWidth() / 2.0, cp5::canvasHeight() / 2.0);
        ctx.scale(solvedZoom, solvedZoom);
        ctx.translate(-camX, -camY);
    }
    if (spawnedThisConnection) {
        if (config::renDebugCollisions) renderDebugCollisions(ctx, now);
        if (config::renNames) renderNames(ctx, sim, now);
        if (config::renHealthBars) renderHealthBars(ctx, sim, now);
    }
    ctx.restore();
}

// World AABB of a shape and every shape below it: circles +-size, polygons +-size*sqrt2,
// barrels by their rotated corners. Strokes are left out here.
void GameRendering::subtreeBounds(Entity& e, double now, const Frame& f, double box[4]) {
    double minX = f.x, minY = f.y, maxX = f.x, maxY = f.y;
    int sides = (int)e.physics->sides;
    double size = e.physics->size.getSmooth(now);
    if (sides == 2) {
        double width = e.physics->width.getSmooth(now);
        double wide = (e.physics->flags & PhysicsFlags::isTrapezoid) ? width * 0.875 : width / 2;
        double ca = std::cos(f.angle), sa = std::sin(f.angle);
        double xs[4] = {-size / 2, -size / 2, size / 2, size / 2};
        double ys[4] = {-width / 2, width / 2, -wide, wide};
        for (int k = 0; k < 4; ++k) {
            double rx = f.x + xs[k] * ca - ys[k] * sa, ry = f.y + xs[k] * sa + ys[k] * ca;
            minX = std::min(minX, rx); maxX = std::max(maxX, rx);
            minY = std::min(minY, ry); maxY = std::max(maxY, ry);
        }
    } else if (sides != 0) {
        double r = sides == 1 ? size : size * 1.414214;
        minX -= r; minY -= r; maxX += r; maxY += r;
    }
    auto it = sceneChildren.find(&e);
    if (it != sceneChildren.end()) {
        for (const std::vector<Entity*>* list : {&it->second.before, &it->second.after}) {
            for (Entity* c : *list) {
                if (!c->position || !c->physics) continue;
                double cb[4];
                subtreeBounds(*c, now, frameOf(*c, now, f), cb);
                minX = std::min(minX, cb[0]); minY = std::min(minY, cb[1]);
                maxX = std::max(maxX, cb[0] + cb[2]); maxY = std::max(maxY, cb[1] + cb[3]);
            }
        }
    }
    box[0] = minX; box[1] = minY; box[2] = maxX - minX; box[3] = maxY - minY;
}

// One red box per drawn shape: its subtree bounds, padded by the widest border among it
// and its below-parent children. Off-screen boxes are skipped.
void GameRendering::renderDebugCollisions(cp5::Context& ctx, Entity& e, double now, const Frame& parent) {
    if (!e.position || !e.style || !e.physics) return;
    Frame f = frameOf(e, now, parent);
    double box[4];
    subtreeBounds(e, now, f, box);
    double pad = e.style->borderWidth.getSmooth(now);
    auto it = sceneChildren.find(&e);
    if (it != sceneChildren.end())
        for (Entity* c : it->second.before) pad = std::max(pad, (double)c->style->borderWidth.getSmooth(now));
    box[0] -= pad; box[1] -= pad; box[2] += 2 * pad; box[3] += 2 * pad;
    if (box[0] + box[2] < screenRectX || box[0] > screenRectX + screenRectW ||
        box[1] + box[3] < screenRectY || box[1] > screenRectY + screenRectH) return;
    ctx.strokeRect(box[0] + 0.5, box[1] + 0.5, box[2], box[3]);
    if (it != sceneChildren.end()) {
        for (const std::vector<Entity*>* list : {&it->second.before, &it->second.after})
            for (Entity* c : *list) renderDebugCollisions(ctx, *c, now, f);
    }
}

// Every physics shape goes in by its subtree bounds, then the tree merges.
void GameRendering::updateDebugTree(Simulation& sim, double now) {
    if (sim.sceneDirty) {
        rebuildScene(sim);
        sim.sceneDirty = false;
    }
    Entity* arena = sim.getArena();
    if (arena && arena->arena) {
        const ArenaData& a = *arena->arena;
        debugTree.setArena((float)a.leftX, (float)a.topY, (float)a.rightX, (float)a.bottomY);
    }
    for (Entity* e : sceneRoots) placeInDebugTree(*e, now, Frame{});
    debugTree.prune();
}

void GameRendering::placeInDebugTree(Entity& e, double now, const Frame& parent) {
    if (!e.position) return;
    Frame f = frameOf(e, now, parent);
    if (e.physics) {
        double box[4];
        subtreeBounds(e, now, f, box);
        debugTree.update(&e, (float)box[0], (float)box[1], (float)box[2], (float)box[3]);
    }
    auto it = sceneChildren.find(&e);
    if (it == sceneChildren.end()) return;
    for (const std::vector<Entity*>* list : {&it->second.before, &it->second.after})
        for (Entity* c : *list) placeInDebugTree(*c, now, f);
}

// Leaves only: the depth as a label, then the node rect, both in screen coordinates.
void GameRendering::renderDebugTree(cp5::Context& ctx, const QuadTree::Node& node) {
    if (node.children) {
        for (int i = 0; i < 4; ++i) renderDebugTree(ctx, node.children[i]);
        return;
    }
    double w = cp5::canvasWidth(), h = cp5::canvasHeight();
    double sx = (node.x - camX) * solvedZoom + w / 2, sy = (node.y - camY) * solvedZoom + h / 2;
    double sw = node.w * solvedZoom, sh = node.h * solvedZoom;
    ctx.setAlpha(0.1);
    drawLabel(ctx, std::to_string(node.depth), sx + sw / 2, sy + sh / 2, node.h * 0.1 * solvedZoom, 0xffffff);
    ctx.setAlpha(0.5);
    ctx.strokeRect(sx + 0.5, sy + 0.5, sw, sh);
}

void GameRendering::renderDebugCollisions(cp5::Context& ctx, double now) {
    ctx.save();
    ctx.setStrokeColor(0xff0000);
    ctx.setLineWidth(1);
    for (Entity* e : sceneRoots) renderDebugCollisions(ctx, *e, now, Frame{});
    ctx.restore();
}

// ---- Off-screen arrows ----

void GameRendering::renderArrow(cp5::Context& ctx, const std::string& text, double tx, double ty, uint32_t color,
                                WorldRenderer* gl) {
    double ui = std::max(cp5::canvasWidth() / 1920.0, cp5::canvasHeight() / 1080.0) * config::uiScale;
    double m = 50 * ui / solvedZoom;
    double minX = screenRectX + m, maxX = screenRectX + screenRectW - m;
    double minY = screenRectY + m, maxY = screenRectY + screenRectH - m;
    if (minX > maxX) std::swap(minX, maxX);
    if (minY > maxY) std::swap(minY, maxY);
    double cx = std::min(std::max(tx, minX), maxX);
    double cy = std::min(std::max(ty, minY), maxY);
    if (cx == tx && cy == ty) return;

    double dx = tx - cx, dy = ty - cy;
    double alpha = std::min(1.0, std::max(0.0, (std::sqrt(dx * dx + dy * dy) - 2700.0) / 100.0)) * 0.35;
    if (alpha <= 0) return;

    if (gl) {
        // The label is rastered at about the device scale, then drawn rotated where the canvas
        // path would draw it: origin (-width / 2, 48) in the rotated frame.
        ArrowLabel& label = arrowLabels[text];
        double scale = std::ceil(solvedZoom * 16) / 16;
        bool changed = !label.canvas || label.scale != scale;
        if (changed) {
            if (!label.canvas) label.canvas = std::make_unique<cp5::Context>(true);
            cp5::Context& c = *label.canvas;
            double size = 25 * scale;
            c.setFontSize(size);
            double textWidth = c.measureText(text);
            int margin = (int)std::ceil(size * 0.2) + 2;
            c.setSize((int)std::ceil(textWidth) + 2 * margin, (int)std::ceil(size * 1.5) + 2 * margin);
            c.clear();
            c.resetTransform();
            c.setAlpha(1);
            drawLabel(c, text, margin + textWidth / 2, c.height() / 2.0, size, 0xffffff);
            label.scale = scale;
            label.width = textWidth / scale;
            label.margin = margin;
        }
        double left = -label.width / 2 - label.margin / scale, top = 48 - label.canvas->height() / 2.0 / scale;
        double right = left + label.canvas->width() / scale, bottom = top + label.canvas->height() / scale;
        double angle = std::atan2(dy, dx), ca = std::cos(angle + M_PI / 2), sa = std::sin(angle + M_PI / 2);
        double corners[8];
        auto corner = [&](int i, double lx, double ly) {
            corners[i * 2] = cx + ca * lx - sa * ly;
            corners[i * 2 + 1] = cy + sa * lx + ca * ly;
        };
        corner(0, left, top);
        corner(1, right, top);
        corner(2, left, bottom);
        corner(3, right, bottom);
        gl->image(*label.canvas, changed, corners, alpha);
        // The triangle below: a polygon of radius 50 with a vertex along the angle.
        gl->shape(3, 0, 0, 50 / 1.414214, 0, 0, cx, cy, angle, color, color, alpha);
        return;
    }

    ctx.save();
    ctx.setAlpha(alpha);
    ctx.translate(cx, cy);
    ctx.rotate(std::atan2(dy, dx));

    ctx.save();
    ctx.rotate(M_PI / 2);
    drawLabel(ctx, text, 0, 48, 25, 0xffffff);
    ctx.restore();

    ctx.scale(50, 50);
    ctx.beginPath();
    ctx.moveTo(-0.5, 0.866025);
    ctx.lineTo(-0.5, -0.866025);
    ctx.lineTo(1, 0);
    ctx.closePath();
    ctx.setFillColor(color);
    ctx.fill();
    ctx.restore();
}

void GameRendering::renderLeaderArrow(cp5::Context& ctx, Simulation& sim, double now, WorldRenderer* gl) {
    (void)now;
    Entity* arena = sim.getArena();
    if (!arena || !arena->arena) return;
    ArenaData& a = *arena->arena;
    if (!(a.flags & ArenaFlags::showsLeaderArrow)) return;

    float k = (float)(1.0 - std::pow(0.9, frameTime / 16.666667));
    if (!leaderInit) {
        leaderInit = true;
        leaderSmoothX = (float)a.leaderX;
        leaderSmoothY = (float)a.leaderY;
    } else {
        leaderSmoothX += ((float)a.leaderX - leaderSmoothX) * k;
        leaderSmoothY += ((float)a.leaderY - leaderSmoothY) * k;
    }
    renderArrow(ctx, "Leader", leaderSmoothX, leaderSmoothY, 0x000000, gl);
}

void GameRendering::renderMothershipArrows(cp5::Context& ctx, Simulation& sim, double now, WorldRenderer* gl) {
    (void)now;
    float k = (float)(1.0 - std::pow(0.9, frameTime / 16.666667));
    for (auto& [id, e] : sim.entities) {
        if (!e.team || !(e.team->flags & TeamFlags::hasMothership)) continue;
        auto it = mothershipSmooth.find(id);
        if (it == mothershipSmooth.end()) {
            it = mothershipSmooth.insert({id, {e.team->mothershipX, e.team->mothershipY}}).first;
        } else {
            it->second.first += (e.team->mothershipX - it->second.first) * k;
            it->second.second += (e.team->mothershipY - it->second.second) * k;
        }
        renderArrow(ctx, "Mothership", it->second.first, it->second.second, colorHex(e.team->teamColor), gl);
    }
}

void GameRendering::solveCamera(Simulation& sim, double now) {
    float screenZoom = std::max(cp5::canvasWidth() / 1920.0f, cp5::canvasHeight() / 1080.0f);

    Entity* cam = sim.getCamera();
    float targetX = camX, targetY = camY, targetZoom = fovZoom;
    if (cam && cam->camera) {
        targetZoom = cam->camera->FOV.getSmooth(now);
        targetX = cam->camera->cameraX.getLinear(now);
        targetY = cam->camera->cameraY.getLinear(now);
        Entity* player = sim.get(cam->camera->player);
        if (!(cam->camera->flags & CameraFlags::usesCameraCoords) && player && player->position) {
            targetX = player->position->x.getLinear(now);
            targetY = player->position->y.getLinear(now);
            if (firstSolve) {
                firstSolve = false;
                camX = targetX;
                camY = targetY;
                fovZoom = targetZoom * 1.3f;
            }
        }
    }

    camX = expSmooth(camX, targetX, 0.1f, frameTime);
    camY = expSmooth(camY, targetY, 0.1f, frameTime);
    fovZoom = expSmooth(fovZoom, targetZoom, fovZoom < targetZoom ? 0.05f : 0.1f, frameTime);
    solvedZoom = fovZoom * screenZoom;
}

bool GameRendering::drawGridTile(double alpha) {
    double lineWidth = 1 / solvedZoom;
    if (gridTile && alpha == tileAlpha && lineWidth == tileLineWidth && config::backgroundColor == tileBackground &&
        config::gridColor == tileGridColor) return false;
    if (!gridTile) {
        gridTile = std::make_unique<cp5::Context>(true);
        gridTile->setSize((int)GRID_SIZE, (int)GRID_SIZE);
    }
    tileAlpha = alpha;
    tileLineWidth = lineWidth;
    tileBackground = config::backgroundColor;
    tileGridColor = config::gridColor;
    cp5::Context& tile = *gridTile;
    tile.resetTransform();
    tile.setAlpha(1);
    tile.setFillColor(config::backgroundColor);
    tile.fillRect(0, 0, GRID_SIZE, GRID_SIZE);
    tile.setStrokeColor(config::gridColor);
    tile.setAlpha(alpha);
    tile.setLineWidth(lineWidth);
    tile.beginPath();
    tile.moveTo(0.5, 0.5);
    tile.lineTo(0.5, GRID_SIZE + 0.5);
    tile.moveTo(0.5, 0.5);
    tile.lineTo(GRID_SIZE + 0.5, 0.5);
    tile.stroke();
    return true;
}

void GameRendering::renderGrid(cp5::Context& ctx, WorldRenderer* gl) {
    int w = cp5::canvasWidth(), h = cp5::canvasHeight();
    gridFade += (float)((1.0 - std::pow(0.95, frameTime / 16.666667)) * ((spawnedThisConnection ? 1.0f : 0.0f) - gridFade));
    gridFade = std::max(0.0f, std::min(1.0f, gridFade));

    if (config::renSolidBackground) {
        // Grid ink folded into the fill: darken by base alpha * (1 - zoom * fade).
        float t = (float)(config::gridBaseAlpha * (1.0 - solvedZoom * gridFade));
        uint32_t color = mix(unpack(config::backgroundColor), {0, 0, 0}, t).pack();
        if (gl) {
            gl->solidBackground(color);
            return;
        }
        ctx.save();
        ctx.resetTransform();
        ctx.setAlpha(1);
        ctx.setFillColor(color);
        ctx.fillRect(0, 0, w, h);
        ctx.restore();
        return;
    }
    // Line alpha scales with zoom (constant ink as line density changes), 1 device px.
    double alpha = config::gridBaseAlpha * solvedZoom * gridFade;
    if (config::renPatternGrid) {
        // One 50x50 tile (background + two lines), repeated under the world scale, so the
        // lines get the pattern's bilinear softening.
        bool changed = drawGridTile(alpha);
        double ox = std::fmod(std::fmod(w * 0.5 / solvedZoom - camX, GRID_SIZE) + GRID_SIZE, GRID_SIZE) - GRID_SIZE;
        double oy = std::fmod(std::fmod(h * 0.5 / solvedZoom - camY, GRID_SIZE) + GRID_SIZE, GRID_SIZE) - GRID_SIZE;
        if (gl) {
            gl->patternBackground(*gridTile, changed, solvedZoom, ox, oy);
            return;
        }
        ctx.save();
        ctx.setAlpha(1);
        ctx.setTransform(solvedZoom, 0, 0, solvedZoom, 0, 0);
        ctx.translate(ox, oy);
        ctx.fillPattern(*gridTile, 0, 0, w / solvedZoom + GRID_SIZE, h / solvedZoom + GRID_SIZE);
        ctx.restore();
        return;
    }
    ctx.save();
    ctx.resetTransform();
    ctx.setAlpha(1);
    ctx.setFillColor(config::backgroundColor);
    ctx.fillRect(0, 0, w, h);

    double cell = GRID_SIZE * solvedZoom;
    if (cell >= 2 && alpha > 0.001) {
        ctx.setAlpha(alpha);
        ctx.setStrokeColor(config::gridColor);
        ctx.setLineWidth(1);
        ctx.setLineCap(cp5::LineCap::butt);
        double offX = std::fmod(std::fmod(w / 2.0 - camX * solvedZoom, cell) + cell, cell);
        double offY = std::fmod(std::fmod(h / 2.0 - camY * solvedZoom, cell) + cell, cell);
        ctx.beginPath();
        for (double x = offX - cell; x < w; x += cell) {
            ctx.moveTo(x + 0.5, 0);
            ctx.lineTo(x + 0.5, h);
        }
        for (double y = offY - cell; y < h; y += cell) {
            ctx.moveTo(0, y + 0.5);
            ctx.lineTo(w, y + 0.5);
        }
        ctx.stroke();
    }
    ctx.restore();
}

void GameRendering::renderBorders(cp5::Context& ctx, Simulation& sim, WorldRenderer* gl) {
    Entity* arena = sim.getArena();
    if (!arena || !arena->arena) return;
    ArenaData& a = *arena->arena;

    if (!bordersInit) {
        bordersInit = true;
        borderMinX = (float)a.leftX;
        borderMinY = (float)a.topY;
        borderMaxX = (float)a.rightX;
        borderMaxY = (float)a.bottomY;
    }
    borderMinX = expSmooth(borderMinX, (float)a.leftX, 0.2f, frameTime);
    borderMinY = expSmooth(borderMinY, (float)a.topY, 0.2f, frameTime);
    borderMaxX = expSmooth(borderMaxX, (float)a.rightX, 0.2f, frameTime);
    borderMaxY = expSmooth(borderMaxY, (float)a.bottomY, 0.2f, frameTime);

    float w = (float)cp5::canvasWidth(), h = (float)cp5::canvasHeight();
    auto sx = [&](float gx) { return (gx - camX) * solvedZoom + w / 2; };
    auto sy = [&](float gy) { return (gy - camY) * solvedZoom + h / 2; };
    float minX = std::max(0.0f, sx(borderMinX));
    float minY = std::max(0.0f, sy(borderMinY));
    float maxX = std::min(w, sx(borderMaxX));
    float maxY = std::min(h, sy(borderMaxY));

    if (gl) {
        gl->screenRect(0, minY, minX, h - minY, config::borderColor, config::borderAlpha);
        gl->screenRect(0, 0, maxX, minY, config::borderColor, config::borderAlpha);
        gl->screenRect(maxX, 0, w - maxX, maxY, config::borderColor, config::borderAlpha);
        gl->screenRect(minX, maxY, w - minX, h - maxY, config::borderColor, config::borderAlpha);
        return;
    }

    ctx.save();
    ctx.resetTransform();
    ctx.setAlpha(config::borderAlpha);
    ctx.setFillColor(config::borderColor);
    ctx.fillRect(0, minY, minX, h - minY);
    ctx.fillRect(0, 0, maxX, minY);
    ctx.fillRect(maxX, 0, w - maxX, maxY);
    ctx.fillRect(minX, maxY, w - minX, h - maxY);
    ctx.restore();
}

void GameRendering::rebuildScene(Simulation& sim) {
    sceneRoots.clear();
    sceneChildren.clear();
    for (auto& [id, e] : sim.entities) {
        Entity* parent = e.relations ? sim.get(e.relations->parent) : nullptr;
        if (parent && e.style) {
            ChildLists& lists = sceneChildren[parent];
            if (e.style->flags & StyleFlags::showsAboveParent) lists.after.push_back(&e);
            else lists.before.push_back(&e);
        }
        if (!e.position || !e.style || parent) continue;
        sceneRoots.push_back(&e);
    }
    std::stable_sort(sceneRoots.begin(), sceneRoots.end(), [](Entity* a, Entity* b) {
        return a->style->zIndex < b->style->zIndex;
    });
    auto childOrder = [](Entity* a, Entity* b) {
        bool af = a->style->flags & StyleFlags::renderFirst;
        bool bf = b->style->flags & StyleFlags::renderFirst;
        if (af != bf) return af;
        return a->style->zIndex < b->style->zIndex;
    };
    for (auto& [parent, lists] : sceneChildren) {
        std::stable_sort(lists.before.begin(), lists.before.end(), childOrder);
        std::stable_sort(lists.after.begin(), lists.after.end(), childOrder);
    }
}

void GameRendering::renderEntities(cp5::Context& ctx, Simulation& sim, double now, WorldRenderer* gl) {
    if (sim.sceneDirty) {
        rebuildScene(sim);
        sim.sceneDirty = false;
    }
    ChildMap& childMap = sceneChildren;
    for (Entity* e : sceneRoots) {
        if (e->physics) {
            double x = e->position->x.getLinear(now), y = e->position->y.getLinear(now);
            double r = std::max(e->physics->size.getSmooth(now), e->physics->width.getSmooth(now)) * 4 + 50;
            if (x + r < screenRectX || x - r > screenRectX + screenRectW ||
                y + r < screenRectY || y - r > screenRectY + screenRectH) continue;
        }
        renderEntity(ctx, sim, *e, now, true, 1.0, Frame{}, childMap, true, gl);
    }
}

double GameRendering::subtreeReach(Entity& e, double now, const ChildMap& childMap) {
    double r = 0;
    if (e.physics) {
        double sz = e.physics->size.getSmooth(now);
        double wd = e.physics->width.getSmooth(now);
        // Polygons draw to a corner radius of size*sqrt2; keep the whole shape in frame.
        r = (int)e.physics->sides >= 3 ? sz * 1.414214 : std::max(sz, wd);
        if (e.style) r += e.style->borderWidth.getSmooth(now);
    }
    auto it = childMap.find(&e);
    if (it != childMap.end()) {
        for (const std::vector<Entity*>* list : {&it->second.before, &it->second.after}) {
            for (Entity* c : *list) {
                if (!c->position) continue;
                double cx = c->position->x.getLinear(now);
                double cy = c->position->y.getLinear(now);
                r = std::max(r, std::hypot(cx, cy) + subtreeReach(*c, now, childMap));
            }
        }
    }
    return r;
}

// World transform of a positioned entity, including the barrel kick.
GameRendering::Frame GameRendering::frameOf(Entity& e, double now, const Frame& parent) {
    double lx = e.position->x.getLinear(now);
    double ly = e.position->y.getLinear(now);
    double la = e.position->angle.getAngle(now);
    // Barrel kick: sine bump over int(clamp(reload,1,7)) ticks, amplitude 5 * width/42.
    if (e.barrel && e.physics && e.barrel->lastFiredTime > -1e17) {
        double dur = (double)(int)std::min(7.0, std::max(1.0, e.barrel->reloadTime));
        double t = (now - e.barrel->lastFiredTime) / 40.0 / dur;
        if (t >= 0 && t < 1) {
            double off = std::sin(t * M_PI) * 5.0 * (e.physics->width.getSmooth(now) / -42.0);
            double a = la + e.barrel->trapezoidDirection;
            lx += std::cos(a) * off;
            ly += std::sin(a) * off;
        }
    }
    Frame self;
    self.present = true;
    if (parent.present) {
        double c = std::cos(parent.angle), s = std::sin(parent.angle);
        self.x = parent.x + c * lx - s * ly;
        self.y = parent.y + s * lx + c * ly;
        self.angle = (e.position->flags & PositionFlags::absoluteRotation) ? la : parent.angle + la;
        self.rootDamageTime = parent.rootDamageTime;
        self.rootFlashing = parent.rootFlashing;
        self.rootFlagsTime = parent.rootFlagsTime;
    } else {
        self.x = lx;
        self.y = ly;
        self.angle = la;
        self.rootDamageTime = e.lastDamageTime;
        self.rootFlashing = e.style && (e.style->flags & StyleFlags::isFlashing);
        self.rootFlagsTime = e.style ? e.style->flagsTime : -1e18;
    }
    return self;
}

GameRendering::Frame GameRendering::worldFrame(Simulation& sim, Entity& e, double now) {
    Entity* parent = e.relations ? sim.get(e.relations->parent) : nullptr;
    return frameOf(e, now, parent && parent->position ? worldFrame(sim, *parent, now) : Frame{});
}

void GameRendering::renderEntity(cp5::Context& ctx, Simulation& sim, Entity& e, double now, bool applyAlpha, double strokeScale,
                                 Frame parent, const ChildMap& childMap, bool allowComposite, WorldRenderer* gl) {
    if (!e.position || !e.style) return;

    Frame self = frameOf(e, now, parent);

    double alpha = 1;
    if (applyAlpha) {
        alpha = e.style->opacity.getSmooth(now);
        double fadeIn = (now - e.createdTime) / 40.0;
        if (fadeIn < 1) alpha *= std::max(0.0, fadeIn);
    }
    if (alpha <= 0) return;

    cp5::Context* target = &ctx;
    auto it = childMap.find(&e);
    // The composite pass exists so overlapping members fade as one; childless shapes fade directly.
    // Its region math is screen-space, so transformed targets (minimap) must opt out.
    bool composite = allowComposite && alpha < 1 && it != childMap.end();
    bool directFade = alpha < 1 && !composite;
    double regX = 0, regY = 0, regW = 0, regH = 0;
    if (composite) {
        // Composite only the entity's screen AABB; fullscreen composites are too expensive.
        double w = cp5::canvasWidth(), h = cp5::canvasHeight();
        double sx = (self.x - camX) * solvedZoom + w / 2;
        double sy = (self.y - camY) * solvedZoom + h / 2;
        double rr = e.physics ? subtreeReach(e, now, childMap) + 50 : 200;
        rr *= solvedZoom;
        regX = std::max(0.0, sx - rr);
        regY = std::max(0.0, sy - rr);
        regW = std::min(w, sx + rr) - regX;
        regH = std::min(h, sy + rr) - regY;
        if (regW <= 0 || regH <= 0) return;
    }
    if (gl) {
        if (composite) gl->beginComposite(alpha, regX, regY, regW, regH);
        static const ChildLists noChildren;
        const ChildLists& children = it != childMap.end() ? it->second : noChildren;
        for (Entity* child : children.before)
            renderEntity(ctx, sim, *child, now, applyAlpha, strokeScale, self, childMap, allowComposite, gl);
        if (e.style->flags & StyleFlags::isVisible)
            renderShape(ctx, e, now, strokeScale, self.x, self.y, self.angle, self, gl, directFade ? alpha : 1);
        for (Entity* child : children.after)
            renderEntity(ctx, sim, *child, now, applyAlpha, strokeScale, self, childMap, allowComposite, gl);
        if (composite) gl->endComposite();
        return;
    }
    if (composite) {
        if (scratchDepth >= scratchPool.size()) scratchPool.push_back(std::make_unique<cp5::Context>(true));
        target = scratchPool[scratchDepth].get();
        ++scratchDepth;
        target->setSize(cp5::canvasWidth(), cp5::canvasHeight());
        target->clearRegion(regX, regY, regW, regH);
        target->save();
        target->setTransform(1, 0, 0, 1, 0, 0);
        target->translate(cp5::canvasWidth() / 2.0, cp5::canvasHeight() / 2.0);
        target->scale(solvedZoom, solvedZoom);
        target->translate(-camX, -camY);
    }

    static const ChildLists noChildren;
    const ChildLists& children = it != childMap.end() ? it->second : noChildren;

    if (directFade) { target->save(); target->scaleAlpha(alpha); }
    for (Entity* child : children.before) renderEntity(*target, sim, *child, now, applyAlpha, strokeScale, self, childMap, allowComposite);
    if (e.style->flags & StyleFlags::isVisible) renderShape(*target, e, now, strokeScale, self.x, self.y, self.angle, self);
    for (Entity* child : children.after) renderEntity(*target, sim, *child, now, applyAlpha, strokeScale, self, childMap, allowComposite);
    if (directFade) target->restore();

    if (composite) {
        target->restore();
        --scratchDepth;
        ctx.save();
        ctx.resetTransform();
        ctx.scaleAlpha(alpha);
        ctx.drawContext(*target, regX, regY, regW, regH, regX, regY, regW, regH);
        ctx.restore();
    }
}

void GameRendering::renderShape(cp5::Context& ctx, Entity& e, double now, double strokeScale, double x, double y, double angle,
                                const Frame& root, WorldRenderer* gl, double alpha) {
    if (!e.physics) return;
    double rootDamageTime = root.rootDamageTime;

    double size = e.physics->size.getSmooth(now);
    int sides = (int)e.physics->sides;
    double bw = e.style->borderWidth.getSmooth(now) * strokeScale;

    RGB fill = unpack(colorHex(e.style->color));
    RGB strokeC = config::renStrokeSoftColor ? mix(fill, {0, 0, 0}, (float)config::strokeIntensity)
                                             : unpack(config::strokeSolidColor);

    double flash = std::max(0.0, std::min(1.0, (120.0 - (now - rootDamageTime)) / 120.0));
    if (flash > 0) {
        // Fill and stroke blend toward the SAME target: white for the first 25%, red after.
        RGB target = flash >= 0.75 ? RGB{255, 255, 255} : RGB{255, 0, 0};
        fill = mix(fill, target, (float)flash);
        strokeC = mix(strokeC, target, (float)flash);
    }
    // Invuln blink lightens only the fill, from the root so the whole tank blinks together.
    if (root.rootFlashing && (((long)((now - root.rootFlagsTime) / 100.0)) & 1)) {
        fill = {fill.r * 0.7f + 76.5f, fill.g * 0.7f + 76.5f, fill.b * 0.7f + 76.5f};
    }

    if (gl) {
        double width = sides == 2 ? e.physics->width.getSmooth(now) : 0;
        gl->shape(sides, e.physics->flags, e.style->flags, size, width, bw, x, y, angle, fill.pack(), strokeC.pack(), alpha);
        return;
    }

    ctx.save();
    ctx.translate(x, y);

    if (sides == 1) {
        ctx.beginPath();
        ctx.arc(0, 0, size, 0, 2 * M_PI);
        ctx.closePath();
        ctx.setFillColor(fill.pack());
        ctx.fill();
        if (bw > 0) {
            ctx.setStrokeColor(strokeC.pack());
            ctx.setLineWidth(bw);
            ctx.stroke();
        }
        ctx.restore();
        return;
    }

    ctx.rotate(angle);
    ctx.beginPath();
    if (sides == 2) {
        double width = e.physics->width.getSmooth(now);
        if (e.physics->flags & PhysicsFlags::isTrapezoid) {
            ctx.moveTo(-size * 0.5, -width * 0.5);
            ctx.lineTo(size * 0.5, -width * 0.875);
            ctx.lineTo(size * 0.5, width * 0.875);
            ctx.lineTo(-size * 0.5, width * 0.5);
            ctx.closePath();
        } else {
            ctx.rect(-size * 0.5, -width * 0.5, size, width);
        }
    } else if (sides >= 3) {
        bool star = e.style->flags & StyleFlags::isStar;
        int count = sides << (star ? 1 : 0);
        double radius = size * 1.414214;
        double offset = count == 4 ? M_PI / 4 : 0;
        for (int i = 0; i < count; ++i) {
            double r = (star && !(i & 1)) ? radius * 0.4 : radius;
            double a = offset + i * 2 * M_PI / count;
            if (i == 0) ctx.moveTo(std::cos(a) * r, std::sin(a) * r);
            else ctx.lineTo(std::cos(a) * r, std::sin(a) * r);
        }
        ctx.closePath();
    } else {
        ctx.restore();
        return;
    }

    ctx.setFillColor(fill.pack());
    ctx.fill();
    if (bw > 0) {
        ctx.setLineJoin(cp5::LineJoin::round);
        ctx.setStrokeColor(strokeC.pack());
        ctx.setLineWidth(bw);
        ctx.stroke();
    }
    ctx.restore();
}

// ---- Names / health bars (world space) ----

static std::string formatScore(double v) {
    char buf[32];
    std::string out;
    if (v < 0) { out = '-'; v = -v; }
    if (v < 1000) std::snprintf(buf, sizeof(buf), "%.0f", v);
    else if (v < 1000000) std::snprintf(buf, sizeof(buf), "%.1fk", v / 1000);
    else std::snprintf(buf, sizeof(buf), "%.1fm", v / 1000000);
    return out + buf;
}

static std::string formatCommas(long long v) {
    std::string s = std::to_string(v < 0 ? -v : v);
    for (int i = (int)s.size() - 3; i > 0; i -= 3) s.insert((size_t)i, ",");
    return (v < 0 ? "-" : "") + s;
}

void GameRendering::drawLabel(cp5::Context& ctx, const std::string& text, double cx, double cy, double size, uint32_t fill,
                              bool stroke, uint32_t strokeColor, double strokeRatio) {
    if (text.empty() || size <= 0) return;
    ctx.setFontSize(size);
    ctx.setTextBaseline(cp5::Baseline::middle);
    double w = ctx.measureText(text);
    ctx.setFillColor(fill);
    if (stroke) {
        ctx.setLineWidth(size * strokeRatio);
        ctx.setStrokeColor(strokeColor);
        ctx.fillStrokeText(text, cx - w / 2, cy);
    } else {
        ctx.fillText(text, cx - w / 2, cy);
    }
}

GameRendering::TextBox GameRendering::textBox(cp5::Context& ctx, const std::string& text, double size,
                                              double strokeRatio) {
    TextBox box;
    box.margin = (int)(2 * strokeRatio * (float)size);
    ctx.setFontSize(size);
    box.w = std::max(1, (int)(ctx.measureText(text) + 2 * box.margin));
    box.h = std::max(1, (int)(float)((float)size * 1.4));
    return box;
}

void GameRendering::drawTextBox(cp5::Context& ctx, const std::string& text, const TextBox& box, double left,
                                double top, double size, uint32_t fill, bool stroke, uint32_t strokeColor,
                                double strokeRatio) {
    if (text.empty() || size <= 0) return;
    ctx.setFontSize(size);
    ctx.setTextBaseline(cp5::Baseline::middle);
    ctx.setFillColor(fill);
    double x = left + box.margin, y = top + box.h / 2;
    if (stroke) {
        ctx.setLineWidth(size * strokeRatio);
        ctx.setStrokeColor(strokeColor);
        ctx.fillStrokeText(text, x, y);
    } else {
        ctx.fillText(text, x, y);
    }
}

void GameRendering::renderNames(cp5::Context& ctx, Simulation& sim, double now) {
    Entity* camera = sim.getCamera();
    Entity* self = camera && camera->camera ? sim.get(camera->camera->player) : nullptr;
    Entity* arena = sim.getArena();
    bool hiddenScores = arena && arena->arena && (arena->arena->flags & ArenaFlags::hiddenScores);

    for (auto& [id, e] : sim.entities) {
        if (!e.name || !e.position || !e.physics || !e.style) continue;
        if (e.name->flags & NameFlags::hiddenName) continue;
        if (&e == self) continue;
        int sides = (int)e.physics->sides;
        if (sides == 2) continue;

        double x = e.position->x.getLinear(now);
        double y = e.position->y.getLinear(now);
        double size = e.physics->size.getSmooth(now);
        double r = sides == 1 ? size : size * 1.414214;

        bool onScreen = x + r > screenRectX && x - r < screenRectX + screenRectW &&
                        y + r > screenRectY && y - r < screenRectY + screenRectH;
        e.name->fade = onScreen ? expSmooth(e.name->fade, 1, 0.2f, frameTime) : expSmooth(e.name->fade, 0, 0.1f, frameTime);

        double alpha = e.style->opacity.getSmooth(now) * e.name->fade;
        if (alpha < 0.01) continue;

        double yTop = y - 1.5 * r;
        double textScale = size / 50.0;
        ctx.save();
        ctx.setAlpha(alpha);
        uint32_t fill = (e.name->flags & NameFlags::highlightedName) ? 0xffff90 : 0xf5f5f5;
        if (e.score && !hiddenScores) {
            std::string s = formatScore(e.score->score.getSmooth(now));
            drawLabel(ctx, s, x, yTop, 25 * textScale, fill, true, 0x555555);
            ctx.setFontSize(25 * textScale);
            ctx.setLineWidth(25 * textScale * 0.2);
            double tw, th;
            ctx.labelSize(s, &tw, &th);
            yTop -= th;
        }
        drawLabel(ctx, e.name->name, x, yTop, 40 * textScale, fill, true, 0x555555);
        ctx.restore();
    }
}

void GameRendering::renderHealthBars(cp5::Context& ctx, Simulation& sim, double now) {
    Entity* camera = sim.getCamera();
    Entity* self = camera && camera->camera ? sim.get(camera->camera->player) : nullptr;

    for (auto& [id, e] : sim.entities) {
        if (!e.health || !e.position || !e.physics || !e.style) continue;
        if (e.health->flags & HealthFlags::hiddenHealthbar) continue;

        double health = e.health->health.getSmooth(now);
        double maxHealth = e.health->maxHealth.getSmooth(now);
        float& fade = e.health->barFade;
        if (health < maxHealth) fade = std::min(1.0f, fade + frameTime / 50.0f);
        else fade = std::max(0.0f, fade - frameTime / 300.0f);

        double opacity = &e == self ? 1.0 : e.style->opacity.getSmooth(now);
        double alpha = opacity * fade;
        if (alpha < 0.01) continue;

        // Where the entity draws, so the own tank's bar moves with its body.
        Frame f = worldFrame(sim, e, now);
        double x = f.x, y = f.y;
        double size = e.physics->size.getSmooth(now);
        int sides = (int)e.physics->sides;
        double barWidth = 2 * size;
        double offset = sides == 2 ? e.physics->width.getSmooth(now) + 25 : size + 35;
        double cullR = size + offset + 50;
        if (x + cullR < screenRectX || x - cullR > screenRectX + screenRectW ||
            y + cullR < screenRectY || y - cullR > screenRectY + screenRectH) continue;
        double ratio = maxHealth > 0 ? std::max(0.0, std::min(1.0, health / maxHealth)) : 0;

        ctx.save();
        ctx.setAlpha(alpha);
        ctx.setLineCap(cp5::LineCap::round);
        ctx.beginPath();
        ctx.moveTo(x - barWidth / 2, y + offset);
        ctx.lineTo(x + barWidth / 2, y + offset);
        ctx.setLineWidth(16);
        ctx.setStrokeColor(config::healthBackgroundColor);
        ctx.stroke();
        ctx.beginPath();
        ctx.moveTo(x - barWidth / 2, y + offset);
        ctx.lineTo(x - barWidth / 2 + barWidth * ratio, y + offset);
        ctx.setLineWidth(10);
        ctx.setStrokeColor(config::healthFillColor);
        ctx.stroke();
        if (config::renRawHealthValues) {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%.1f", health);
            drawLabel(ctx, buf, x, y + offset, 15, 0xffffff);
        }
        ctx.restore();
    }
}

// ---- HUD (screen space) ----

float GameRendering::uiBase() {
    return std::max((float)cp5::canvasWidth() / 1920.0f, (float)cp5::canvasHeight() / 1080.0f);
}

float GameRendering::uiUnits(float n) {
    return (float)(config::uiScale * (double)(uiBase() * n));
}

// A round-capped stroke from left + 0.5 over max(w - h, 1).
void GameRendering::drawBar(cp5::Context& ctx, double perc, double cx, double cy, double w, double h, uint32_t fill) {
    float inner = std::max((float)w - (float)h, 1.0f);
    float x = (float)cx + inner * -0.5f, y = (float)cy + 0.5f;
    float clamped = perc < 0 ? 0.0f : perc > 1 ? 1.0f : (float)perc;
    ctx.setLineCap(cp5::LineCap::round);
    ctx.beginPath();
    ctx.moveTo(x + 0.5f, y);
    ctx.lineTo(inner + x + 0.5f, y);
    ctx.setLineWidth((float)h);
    ctx.setStrokeColor(config::barBackgroundColor);
    ctx.stroke();
    ctx.beginPath();
    ctx.moveTo(x + 0.5f, y);
    ctx.lineTo(x + std::max(inner * clamped, 1.0f) + 0.5f, y);
    ctx.setLineWidth((double)(float)h * 0.75);
    ctx.setStrokeColor(fill);
    ctx.stroke();
}

namespace {
    // A fake-entity piece for the icon builder; coordinates in tank space (body size 50).
    struct PreviewPart {
        int sides;
        double x, y, angle, size, width;
        bool trapezoid;
        uint32_t color;
        bool aboveBody;
        bool taper = false; // sides-2 reverse-taper (tip 0.75x); overrides trapezoid flare
    };

    void previewGuard(std::vector<PreviewPart>& parts, double sides, double ratio, double offsetAngle) {
        parts.push_back({(int)sides, 0, 0, offsetAngle, 50 * ratio * M_SQRT1_2, 0, false, colorHex(Color::Border), false});
    }

    // Ring auto turrets sit beneath the body; single auto turrets render above.
    void previewAutoTurret(std::vector<PreviewPart>& parts, double x, double y, double angle, bool above) {
        uint32_t barrel = colorHex(Color::Barrel);
        parts.push_back({2, x + std::cos(angle) * 27.5, y + std::sin(angle) * 27.5, angle, 55, 42 * 0.7, false, barrel, above});
        parts.push_back({1, x, y, 0, 25, 0, false, barrel, above});
    }

    void previewAddon(std::vector<PreviewPart>& parts, const std::string& id) {
        if (id.empty()) return;
        uint32_t barrel = colorHex(Color::Barrel);
        if (id == "dombase") previewGuard(parts, 6, 1.24, 0);
        else if (id == "smasher") previewGuard(parts, 6, 1.15, 0);
        else if (id == "landmine") { previewGuard(parts, 6, 1.15, 0); previewGuard(parts, 6, 1.15, 0); }
        else if (id == "spike") {
            previewGuard(parts, 3, 1.3, 0);
            previewGuard(parts, 3, 1.3, M_PI / 3);
            previewGuard(parts, 3, 1.3, M_PI / 6);
            previewGuard(parts, 3, 1.3, M_PI / 2);
        } else if (id == "launcher") {
            double size = 65.5 * M_SQRT2;
            parts.push_back({2, size / 2, 0, 0, size, 33.6, true, barrel, false});
        } else if (id == "pronounced") {
            parts.push_back({2, 40, 0, M_PI, 50, 42, true, barrel, false});
        } else if (id == "dompronounced") {
            parts.push_back({2, 50, 0, M_PI, 22, 35, true, barrel, false});
        } else if (id == "autoturret") {
            previewAutoTurret(parts, 0, 0, 0, true);
        } else if (id == "autosmasher") {
            previewGuard(parts, 6, 1.15, 0);
            previewAutoTurret(parts, 0, 0, 0, true);
        } else if (id == "auto2" || id == "auto3" || id == "auto5" || id == "auto7") {
            int n = id == "auto2" ? 2 : id == "auto3" ? 3 : id == "auto7" ? 7 : 5;
            for (int i = 0; i < n; ++i) {
                double a = 2 * M_PI * i / n;
                previewAutoTurret(parts, std::cos(a) * 40, std::sin(a) * 40, a, false);
            }
        }
    }
}

void GameRendering::updatePreviewLook() {
    PreviewLook look{colorHex(Color::Border), colorHex(Color::Barrel), config::strokeSolidColor, config::strokeIntensity,
                     config::renStrokeSoftColor, config::renDebugCollisions, tankdefs::version()};
    if (look == previewLook) return;
    previewLook = look;
    ++previewLookId;
}

void GameRendering::drawTankIcon(cp5::Context& ctx, TankIcon& icon, int tankId, double cx, double cy, double angle,
                                 double scale, bool centerAroundAABB, bool render) {
    if (icon.tankId != tankId || icon.scale != scale || icon.look != previewLookId) {
        if (!render) return;
        renderTankIcon(icon, tankId, scale);
    }
    double w = icon.w, h = icon.h;
    ctx.save();
    ctx.translate(cx, cy);
    ctx.rotate(angle);
    // The upgrade panel centers the AABB on the box, so the body sits off-center. Otherwise
    // the tank origin lands on the anchor.
    double x = centerAroundAABB ? -w / 2 : icon.centerX - w / 2;
    double y = centerAroundAABB ? -h / 2 : icon.centerY - h / 2;
    ctx.drawContext(*icon.canvas, 0, 0, w, h, x, y, w, h);
    ctx.restore();
}

void GameRendering::renderTankIcon(TankIcon& icon, int tankId, double scale) {
    std::vector<PreviewPart> parts;
    previewAddon(parts, tankdefs::preAddon(tankId));

    float barrels[16 * 8];
    int count = tankdefs::barrels(tankId, barrels, 16);
    uint32_t barrelColor = colorHex(Color::Barrel);
    for (int i = 0; i < count; ++i) {
        float* b = barrels + i * 8;
        double a = b[0], size = b[1], width = b[2] * 42, offset = b[3];
        double dir = b[5], dist = b[6];
        double bx = std::cos(a) * (size / 2 + dist) - std::sin(a) * offset;
        double by = std::sin(a) * (size / 2 + dist) + std::cos(a) * offset;
        parts.push_back({2, bx, by, a + dir, size, width, b[4] != 0, barrelColor, false});
        if (b[7] != 0) {
            double sz = width * (20.0 / 42);
            parts.push_back({2, bx + std::cos(a) * (size + sz) / 2, by + std::sin(a) * (size + sz) / 2,
                             a, sz, width, true, barrelColor, false});
        }
    }
    previewAddon(parts, tankdefs::postAddon(tankId));

    // Polygon bodies use a smaller base size (sides 4 -> sqrt2*32.5, sides 16 -> sqrt2*25).
    int bodySides = tankdefs::bodySides(tankId);
    double bodySize = bodySides == 4 ? M_SQRT2 * 32.5 : bodySides == 16 ? M_SQRT2 * 25 : 50;
    PreviewPart body{bodySides, 0, 0, 0, bodySize, 0, false, 0x00b2e1, false};

    // AABB: body rect union polygon rects and rotated barrel corners, strokes excluded.
    auto partBounds = [](const PreviewPart& p, double& minX, double& minY, double& maxX, double& maxY) {
        if (p.sides == 2) {
            double ca = std::cos(p.angle), sa = std::sin(p.angle);
            double wide = p.trapezoid ? p.width * 0.875 : p.width / 2;
            double xs[4] = {-p.size / 2, -p.size / 2, p.size / 2, p.size / 2};
            double ys[4] = {-p.width / 2, p.width / 2, -wide, wide};
            for (int k = 0; k < 4; ++k) {
                double rx = p.x + xs[k] * ca - ys[k] * sa;
                double ry = p.y + xs[k] * sa + ys[k] * ca;
                minX = std::min(minX, rx); maxX = std::max(maxX, rx);
                minY = std::min(minY, ry); maxY = std::max(maxY, ry);
            }
        } else {
            double r = p.sides == 1 ? p.size : p.size * 1.414214;
            minX = std::min(minX, p.x - r); maxX = std::max(maxX, p.x + r);
            minY = std::min(minY, p.y - r); maxY = std::max(maxY, p.y + r);
        }
    };
    double minX = 0, minY = 0, maxX = 0, maxY = 0;
    partBounds(body, minX, minY, maxX, maxY);
    for (const PreviewPart& p : parts) partBounds(p, minX, minY, maxX, maxY);

    // The AABB sits in the middle of the canvas, with room for the strokes and debug boxes
    // all around.
    double pad = 10 + std::ceil(7.5 * scale);
    int w = (int)std::ceil((maxX - minX) * scale + 2 * pad), h = (int)std::ceil((maxY - minY) * scale + 2 * pad);
    if (!icon.canvas) icon.canvas = std::make_unique<cp5::Context>(true);
    cp5::Context& ctx = *icon.canvas;
    // Firefox only accelerates canvases of at least 128x128, and drawing smaller ones into
    // accelerated ones every frame can get those moved to software.
    ctx.setSize(std::max(w, 128), std::max(h, 128));
    ctx.clear();
    icon.w = w;
    icon.h = h;
    icon.tankId = tankId;
    icon.scale = scale;
    icon.look = previewLookId;
    icon.centerX = (minX + maxX) / 2 * scale;
    icon.centerY = (minY + maxY) / 2 * scale;

    double bwOuter = 7.5 * scale;
    auto drawPart = [&](const PreviewPart& p) {
        double bw = bwOuter;
        RGB fill = unpack(p.color);
        uint32_t stroke = config::renStrokeSoftColor ? mix(fill, {0, 0, 0}, (float)config::strokeIntensity).pack()
                                                     : config::strokeSolidColor;
        ctx.save();
        ctx.translate(p.x * scale, p.y * scale);
        if (p.sides == 1) {
            double r = p.size * scale;
            ctx.beginPath();
            ctx.arc(0, 0, r + bw / 2, 0, 2 * M_PI);
            ctx.setFillColor(stroke);
            ctx.fill();
            ctx.beginPath();
            ctx.arc(0, 0, r - bw / 2, 0, 2 * M_PI);
            ctx.setFillColor(p.color);
            ctx.fill();
            ctx.restore();
            return;
        }
        ctx.rotate(p.angle);
        ctx.beginPath();
        if (p.sides == 2) {
            double length = p.size * scale, width = p.width * scale;
            if (p.taper) {
                ctx.moveTo(-length / 2, -width / 2);
                ctx.lineTo(length / 2, -width * 0.375);
                ctx.lineTo(length / 2, width * 0.375);
                ctx.lineTo(-length / 2, width / 2);
                ctx.closePath();
            } else if (p.trapezoid) {
                ctx.moveTo(-length / 2, -width / 2);
                ctx.lineTo(length / 2, -width * 0.875);
                ctx.lineTo(length / 2, width * 0.875);
                ctx.lineTo(-length / 2, width / 2);
                ctx.closePath();
            } else {
                ctx.rect(-length / 2, -width / 2, length, width);
            }
        } else {
            double r = p.size * 1.414214 * scale;
            for (int k = 0; k < p.sides; ++k) {
                double va = 2 * M_PI * k / p.sides + (p.sides == 4 ? M_PI / 4 : 0);
                if (k == 0) ctx.moveTo(std::cos(va) * r, std::sin(va) * r);
                else ctx.lineTo(std::cos(va) * r, std::sin(va) * r);
            }
            ctx.closePath();
        }
        ctx.setFillColor(p.color);
        ctx.fill();
        ctx.setStrokeColor(stroke);
        ctx.setLineWidth(bw);
        ctx.setLineJoin(cp5::LineJoin::round);
        ctx.stroke();
        ctx.restore();
    };

    ctx.save();
    ctx.translate(w / 2.0 - icon.centerX, h / 2.0 - icon.centerY);

    for (const PreviewPart& p : parts) if (!p.aboveBody) drawPart(p);
    drawPart(body);
    for (const PreviewPart& p : parts) if (p.aboveBody) drawPart(p);

    if (config::renDebugCollisions) {
        // Same red boxes as the world pass: every part, then the whole tank, padded by the 7.5 border.
        ctx.setStrokeColor(0xff0000);
        ctx.setLineWidth(scale);
        auto box = [&](double x0, double y0, double x1, double y1) {
            ctx.strokeRect((x0 - 7.5 + 0.5) * scale, (y0 - 7.5 + 0.5) * scale, (x1 - x0 + 15) * scale, (y1 - y0 + 15) * scale);
        };
        for (const PreviewPart& p : parts) {
            double x0 = p.x, y0 = p.y, x1 = p.x, y1 = p.y;
            partBounds(p, x0, y0, x1, y1);
            box(x0, y0, x1, y1);
        }
        box(minX, minY, maxX, maxY);
    }
    ctx.restore();
}

void GameRendering::onNewConnection() {
    firstSolve = true;
    bordersInit = false;
    leaderInit = false;
    mothershipSmooth.clear();
    minimapContentTime = -1e18;
    levelBarPerc = 0;
    levelCreep = 0;
    levelBarDrain = false;
    scoreBarPerc = 0;
    for (int i = 0; i < 10; ++i) scoreboardVals[i] = scoreboardHover[i] = 0;
}

void GameRendering::renderHUD(cp5::Context& ctx, Simulation& sim, double now, double mouseX, double mouseY,
                              int playersOnline, const std::string& pingText, bool hudVisible) {
    double w = cp5::canvasWidth(), h = cp5::canvasHeight();
    double ui = std::max(w / 1920.0, h / 1080.0) * config::uiScale;
    double smoothRate = 1.0 - std::pow(0.9, frameTime / 16.666667);

    Entity* camera = sim.getCamera();
    Entity* arena = sim.getArena();
    ArenaData* arenaData = arena && arena->arena ? &*arena->arena : nullptr;
    CameraData* cam = camera && camera->camera ? &*camera->camera : nullptr;
    Entity* player = cam ? sim.get(cam->player) : nullptr;
    bool hiddenScores = arenaData && (arenaData->flags & ArenaFlags::hiddenScores);

    ctx.save();
    ctx.resetTransform();
    ctx.setAlpha(1);

    // hiddenScores only hides the score bar; gamemodes like Tag publish team rows with it set.
    if (hudVisible && config::renScoreboard && arenaData && arenaData->scoreboardAmount > 0) {
        int n = std::min(10, (int)arenaData->scoreboardAmount);
        float colX = (float)w + uiUnits(-130);
        double titleSize = (double)uiUnits(24);
        TextBox titleBox = textBox(ctx, "Scoreboard", titleSize);
        drawTextBox(ctx, "Scoreboard", titleBox, std::round(colX - (float)(titleBox.w / 2)),
                    std::round(uiUnits(32) - (float)(titleBox.h / 2)), titleSize, 0xffffff);
        for (int i = 0; i < n; ++i) {
            scoreboardVals[i] = expSmooth(scoreboardVals[i], (float)arenaData->scoreboardScores[i], 0.1f, frameTime);
        }
        float top = scoreboardVals[0];
        float barW = uiUnits(220), barH = uiUnits(20), inner = barW - barH;
        double rowSize = (double)uiUnits(15);
        float y = uiUnits(32) + (float)titleBox.h;
        for (int i = 0; i < n; ++i, y += uiUnits(24)) {
            bool over = mouseX >= colX - barW * 0.5f && mouseX <= colX + barW * 0.5f &&
                        mouseY >= y - barH * 0.5f && mouseY <= y + barH * 0.5f;
            scoreboardHover[i] = over ? expSmooth(scoreboardHover[i], 1, 0.2f, frameTime) : expSmooth(scoreboardHover[i], 0, 0.1f, frameTime);
            float perc = top <= 0 ? 1.0f : scoreboardVals[i] / top;
            RGB c = unpack(colorHex(arenaData->scoreboardColors[i]));
            c = mix(c, {255, 255, 255}, scoreboardHover[i] * 0.5f);
            drawBar(ctx, perc, colX, y, barW, barH, c.pack());
            if (arenaData->scoreboardTanks[i] >= 0) {
                // Icon rides the unclamped fill end; the leader's icon is flipped 180deg.
                float ix = colX + inner * perc - inner * 0.5f;
                drawTankIcon(ctx, scoreboardIcons[i], arenaData->scoreboardTanks[i], ix, y, i == 0 ? M_PI : 0,
                             (double)uiUnits(1) * 0.13);
            }
            // The row is separate rasters side by side, so the score counts up with the bar.
            std::string segs[4];
            int count = 0;
            if (config::renScoreboardNames && !arenaData->scoreboardNames[i].empty()) {
                segs[count++] = arenaData->scoreboardNames[i];
                segs[count++] = " - ";
            }
            segs[count++] = formatScore(scoreboardVals[i]);
            segs[count++] = arenaData->scoreboardSuffixes[i];
            TextBox boxes[4];
            int width = 0, height = 0;
            for (int s = 0; s < count; ++s) {
                boxes[s] = textBox(ctx, segs[s], rowSize);
                width += boxes[s].w - 2 * boxes[s].margin;
                height = std::max(height, boxes[s].h);
            }
            width += boxes[0].margin + boxes[count - 1].margin;
            float left = colX - (float)(width / 2);
            double rowTop = std::round(y - (float)(height / 2));
            for (int s = 0; s < count; ++s) {
                if (!segs[s].empty()) drawTextBox(ctx, segs[s], boxes[s], std::round(left), rowTop, rowSize, 0xffffff);
                left += (float)(boxes[s].w - 2 * boxes[s].margin);
            }
        }
    }

    // The bars and the player name need a live player.
    if (hudVisible && cam && player) {
        float cx = (float)(cp5::canvasWidth() / 2);
        int levelY = (int)((float)(int)((float)cp5::canvasHeight() + uiUnits(-30)) + uiUnits(-4));
        // The fill creeps at 1/s up to progress/max. A level change runs it on to 1.75 (full),
        // drops it to 0 and holds it there until the bar has drained. A max of 0 (max level)
        // makes the cap inf or NaN, so the bar fills.
        levelCreep += frameTime / 1000.0f;
        if (cam->levelbarFill) {
            if (levelCreep >= 1.75f) {
                cam->levelbarFill = false;
                levelBarDrain = true;
                levelCreep = 0;
            }
        } else if (levelBarDrain) {
            levelCreep = 0;
            if (levelBarPerc < 0.005) levelBarDrain = false;
        } else {
            float cap = (float)cam->levelbarProgress / (float)cam->levelbarMax;
            if (levelCreep > cap) levelCreep = cap;
        }
        levelBarPerc = (levelCreep + levelBarPerc * 9.0f) / 10.0f;
        drawBar(ctx, std::max(0.0f, std::min(1.0f, levelBarPerc)), cx, levelY, uiUnits(424), uiUnits(24), config::xpBarFillColor);
        // The text holds its last value while the fill runs.
        if (!cam->levelbarFill) {
            std::string tank = !cam->tankOverride.empty() ? cam->tankOverride : tankdefs::name(cam->tank);
            levelText = "Lvl " + std::to_string((int)cam->level) + ' ' + tank;
        }
        auto centered = [&](const std::string& text, float y, float size) {
            TextBox box = textBox(ctx, text, size);
            drawTextBox(ctx, text, box, std::round(cx - (float)(box.w / 2)), std::round(y - (float)(box.h / 2)),
                        size, 0xffffff);
        };
        centered(levelText, (float)levelY, uiUnits(16));

        // Hidden scores drop the score bar, and the name moves down onto its anchor.
        int scoreY = (int)((float)levelY + uiUnits(-24)), nameY = scoreY;
        if (!hiddenScores) {
            double leader = arenaData && arenaData->scoreboardAmount > 0 ? scoreboardVals[0] : 0;
            double score = cam->score.getSmooth(now);
            double t = arenaData && arenaData->scoreboardAmount > 0 ? (leader > 0 ? score / leader : 1.0) : 0;
            scoreBarPerc += (float)((t - scoreBarPerc) * smoothRate);
            drawBar(ctx, scoreBarPerc, cx, scoreY, uiUnits(320), uiUnits(20), config::scoreBarFillColor);
            centered("Score: " + formatCommas((long long)score), (float)scoreY, uiUnits(15));
            nameY = (int)((float)scoreY + uiUnits(-20));
        }
        if (player->name && !player->name->name.empty())
            centered(player->name->name, (float)nameY + uiUnits(-12), uiUnits(40));
    }

    if (hudVisible && arenaData) {
        double ms = 175 * ui, margin = 20 * ui;
        double x0 = w - margin - ms, y0 = h - margin - ms;

        // Info stack grows up from the minimap: debug info, ping (L toggles), player count, title.
        if (player) {
            // Each raster's bottom-right corner sits on the (int) anchor, which then steps up by the
            // raster height.
            double right = (int)(w - 20 * ui);
            double bottom = (int)(y0 - 5 * ui);
            auto stackLine = [&](const std::string& text, double size) {
                ctx.setFontSize(size);
                ctx.setLineWidth(size * 0.2);
                double tw, th;
                ctx.labelSize(text, &tw, &th);
                double gw = ctx.measureText(text);
                double margin = (tw - gw) / 2;
                th = (int)th;
                drawLabel(ctx, text, right - margin - gw / 2, bottom - th / 2, size, 0xffffff);
                bottom -= th;
            };
            if (config::renDebugInfo) {
                char buf[128];
                std::snprintf(buf, sizeof(buf), "Entities: %d/%d/%d/%d total/created/updated/destroyed",
                              (int)sim.entities.size(), sim.lastCreated, sim.lastUpdated, sim.lastDeleted);
                stackLine(buf, 16 * ui);
                // The KB figure stays 0.
                std::snprintf(buf, sizeof(buf), "Memory: %d KB / %d ctxs", 0, cp5::contextCount());
                stackLine(buf, 16 * ui);
            }
            if (!pingText.empty()) stackLine(pingText, 16 * ui);
            if (playersOnline >= 0) {
                stackLine(formatCommas(playersOnline) + " players", 16 * ui);
            }
            stackLine("diep.io", 20 * ui);
        }

        ctx.setFillColor(config::minimapBackgroundColor);
        ctx.fillRect(x0, y0, ms, ms);

        double aw = arenaData->rightX - arenaData->leftX;
        double ah = arenaData->bottomY - arenaData->topY;
        if (aw > 0 && ah > 0) {
            // showsOnMap entities (bases, maze walls), refreshed every second.
            int cpx = (int)std::round(ms);
            if (!minimapContent) minimapContent = std::make_unique<cp5::Context>(true);
            bool resized = minimapContent->width() != cpx || minimapContent->height() != cpx;
            if (resized || now - minimapContentTime >= 1000) {
                minimapContentTime = now;
                if (resized) minimapContent->setSize(cpx, cpx);
                minimapContent->clear();
                minimapContent->save();
                minimapContent->scale(cpx / aw, cpx / ah);
                minimapContent->translate(-arenaData->leftX, -arenaData->topY);
                for (Entity* e : sceneRoots) {
                    if (e == player) continue;
                    if (!e->physics || !(e->physics->flags & PhysicsFlags::showsOnMap)) continue;
                    renderEntity(*minimapContent, sim, *e, now, false, 1.0, Frame{}, sceneChildren, false);
                }
                minimapContent->restore();
            }
            ctx.drawContext(*minimapContent, 0, 0, cpx, cpx, x0, y0, ms, ms);
            if (config::renMinimapViewport) {
                auto mapU = [&](double sx) { return (camX + (sx - w / 2) / solvedZoom - arenaData->leftX) / aw; };
                auto mapV = [&](double sy) { return (camY + (sy - h / 2) / solvedZoom - arenaData->topY) / ah; };
                double u0 = std::max(0.0, mapU(0)), v0 = std::max(0.0, mapV(0));
                double u1 = std::min(1.0, mapU(w)), v1 = std::min(1.0, mapV(h));
                if (u1 > u0 && v1 > v0) {
                    ctx.save();
                    ctx.scaleAlpha(0.1);
                    ctx.setFillColor(0x000000);
                    ctx.fillRect(x0 + u0 * ms, y0 + v0 * ms, (u1 - u0) * ms, (v1 - v0) * ms);
                    ctx.restore();
                }
            }
        }

        double px = 0, py = 0, pa = 0;
        bool have = false;
        if (player && player->position) {
            px = player->position->x.getLinear(now);
            py = player->position->y.getLinear(now);
            pa = player->position->angle.getAngle(now);
            have = true;
        } else if (cam) {
            px = cam->cameraX.getLinear(now);
            py = cam->cameraY.getLinear(now);
            have = true;
        }
        if (have && aw > 0 && ah > 0) {
            double u = (px - arenaData->leftX) / aw;
            double v = (py - arenaData->topY) / ah;
            ctx.save();
            ctx.translate(x0 + u * ms, y0 + v * ms);
            ctx.rotate(pa);
            ctx.scale(ms * 0.02, ms * 0.02);
            ctx.setFillColor(0x000000);
            ctx.beginPath();
            ctx.moveTo(1.5, 0);
            ctx.lineTo(-0.75, 0.866025);
            ctx.lineTo(-0.75, -0.866025);
            ctx.closePath();
            ctx.fill();
            ctx.restore();
        }

        ctx.setStrokeColor(config::minimapBorderColor);
        ctx.setLineWidth(0.03 * ms);
        ctx.setLineJoin(cp5::LineJoin::round);
        ctx.strokeRect(x0, y0, ms, ms);
    }

    ctx.restore();
}
