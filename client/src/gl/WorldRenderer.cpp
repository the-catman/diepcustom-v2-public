#include "gl/WorldRenderer.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include <GLES3/gl3.h>
#include <emscripten/emscripten.h>
#include <emscripten/html5.h>

#include "Const.h"
#include "cp5/Context.h"

EM_JS(void, world_show, (int shown), {
    for (const id of ["world", "overlay"]) {
        const canvas = document.getElementById(id);
        if (canvas) canvas.style.display = shown ? "block" : "none";
    }
});

// The base canvas stops being composited while the world canvas paints over all of it. It
// keeps taking input, so opacity rather than visibility.
EM_JS(void, world_cover_base, (int covered), {
    const canvas = document.getElementById("canvas");
    if (canvas) canvas.style.opacity = covered ? "0" : "";
});

EM_JS(void, world_upload_canvas, (int texture, int canvas), {
    const gl = GLctx;
    gl.bindTexture(gl.TEXTURE_2D, GL.textures[texture]);
    gl.pixelStorei(gl.UNPACK_PREMULTIPLY_ALPHA_WEBGL, true);
    gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, gl.RGBA, gl.UNSIGNED_BYTE, Module.cp5.ctxs[canvas].canvas);
    gl.pixelStorei(gl.UNPACK_PREMULTIPLY_ALPHA_WEBGL, false);
});

// A lost context only comes back when the loss event is cancelled.
EM_JS(void, world_keep_restorable, (), {
    const canvas = document.getElementById("world");
    if (canvas) canvas.addEventListener("webglcontextlost", e => e.preventDefault());
});

namespace {
    enum Kind { KindCircle = 0, KindRect = 1, KindTrapezoid = 2, KindPolygon = 3, KindStar = 4 };

    // Instance layout: x y | size width borderWidth angle | kind count param alpha | fill | stroke.
    const char* SHAPE_VS = R"(#version 300 es
precision highp float;
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec4 aDim;
layout(location = 2) in vec4 aShape;
layout(location = 3) in vec4 aFill;
layout(location = 4) in vec4 aStroke;
uniform vec2 uCamera;
uniform float uZoom;
uniform vec2 uScreen;
out vec2 vLocal;
flat out vec4 vDim;
flat out vec4 vShape;
flat out vec4 vFill;
flat out vec4 vStroke;
void main() {
    float size = aDim.x, width = aDim.y;
    int kind = int(aShape.x);
    vec2 ext;
    if (kind == 0) ext = vec2(size);
    else if (kind == 1) ext = vec2(size * 0.5, width * 0.5);
    else if (kind == 2) ext = vec2(size * 0.5, width * max(0.5, aShape.z));
    else ext = vec2(size * 1.414214);
    ext += aDim.z * 0.5 + 1.5 / uZoom;
    vec2 corner = vec2(float(gl_VertexID & 1), float(gl_VertexID >> 1)) * 2.0 - 1.0;
    vec2 local = corner * ext;
    float c = cos(aDim.w), s = sin(aDim.w);
    vec2 world = aPos + vec2(c * local.x - s * local.y, s * local.x + c * local.y);
    vec2 screen = (world - uCamera) * uZoom + uScreen * 0.5;
    gl_Position = vec4(screen.x / uScreen.x * 2.0 - 1.0, 1.0 - screen.y / uScreen.y * 2.0, 0.0, 1.0);
    vLocal = local;
    vDim = aDim;
    vShape = aShape;
    vFill = aFill;
    vStroke = aStroke;
}
)";

    // d is the exact signed distance to the outline, so the stroke band |d| <= borderWidth / 2
    // gets the round joins Canvas2D strokes with. Coverage is the pixel's overlap with each
    // region across the outline. Fill, then stroke over it, each at the shape's alpha.
    const char* SHAPE_FS = R"(#version 300 es
precision highp float;
in vec2 vLocal;
flat in vec4 vDim;
flat in vec4 vShape;
flat in vec4 vFill;
flat in vec4 vStroke;
uniform float uZoom;
out vec4 outColor;
float dot2(vec2 v) { return dot(v, v); }
float trapezoid(vec2 p, float r1, float r2, float he) {
    vec2 k1 = vec2(r2, he);
    vec2 k2 = vec2(r2 - r1, 2.0 * he);
    p.x = abs(p.x);
    vec2 ca = vec2(p.x - min(p.x, p.y < 0.0 ? r1 : r2), abs(p.y) - he);
    vec2 cb = p - k1 + k2 * clamp(dot(k1 - p, k2) / dot2(k2), 0.0, 1.0);
    float s = (cb.x < 0.0 && ca.y < 0.0) ? -1.0 : 1.0;
    return s * sqrt(min(dot2(ca), dot2(cb)));
}
void main() {
    float size = vDim.x, width = vDim.y, half_ = vDim.z * 0.5;
    int kind = int(vShape.x);
    vec2 p = vLocal;
    float d;
    if (kind == 0) {
        d = length(p) - size;
    } else if (kind == 1) {
        vec2 q = abs(p) - vec2(size * 0.5, width * 0.5);
        d = length(max(q, 0.0)) + min(max(q.x, q.y), 0.0);
    } else if (kind == 2) {
        d = trapezoid(vec2(p.y, p.x), width * 0.5, width * vShape.z, size * 0.5);
    } else {
        // Fold into the mirror chamber between a vertex (a = 0) and the next edge midpoint or
        // star tip (a = pi / count); the outline there is one segment.
        float an = 3.14159265 / vShape.y;
        float radius = size * 1.414214;
        float a = mod(atan(p.y, p.x) - (kind == 3 ? vShape.z : 0.0), 2.0 * an);
        if (a > an) a = 2.0 * an - a;
        vec2 q = length(p) * vec2(cos(a), sin(a));
        vec2 A = kind == 3 ? vec2(radius, 0.0) : vec2(radius * vShape.z, 0.0);
        vec2 B = (kind == 3 ? radius * cos(an) : radius) * vec2(cos(an), sin(an));
        vec2 ab = B - A, aq = q - A;
        float dist = length(aq - ab * clamp(dot(aq, ab) / dot(ab, ab), 0.0, 1.0));
        d = ab.x * aq.y - ab.y * aq.x > 0.0 ? -dist : dist;
    }
    float px = 1.0 / uZoom;
    float fillCover = clamp(0.5 - d / px, 0.0, 1.0);
    float strokeCover = clamp((min(d + px * 0.5, half_) - max(d - px * 0.5, -half_)) / px, 0.0, 1.0);
    float fa = fillCover * vShape.w, sa = strokeCover * vShape.w;
    float alpha = sa + fa * (1.0 - sa);
    if (alpha <= 0.0) discard;
    outColor = vec4(vStroke.rgb * sa + vFill.rgb * fa * (1.0 - sa), alpha);
}
)";

    const char* BLIT_VS = R"(#version 300 es
void main() {
    vec2 p = vec2(float((gl_VertexID & 1) << 2), float((gl_VertexID & 2) << 1)) - 1.0;
    gl_Position = vec4(p, 0.0, 1.0);
}
)";

    const char* BLIT_FS = R"(#version 300 es
precision highp float;
uniform sampler2D uTexture;
uniform float uAlpha;
out vec4 outColor;
void main() {
    outColor = texelFetch(uTexture, ivec2(gl_FragCoord.xy), 0) * uAlpha;
}
)";

    // Canvas2D's pattern fill: each device pixel center, mapped back through the transform,
    // samples the repeating tile bilinearly. uMap is (scale, originX, originY).
    const char* PATTERN_FS = R"(#version 300 es
precision highp float;
uniform sampler2D uTexture;
uniform vec3 uMap;
uniform float uHeight;
uniform vec2 uTileSize;
out vec4 outColor;
void main() {
    vec2 device = vec2(gl_FragCoord.x, uHeight - gl_FragCoord.y);
    outColor = texture(uTexture, (device / uMap.x - uMap.yz) / uTileSize);
}
)";

    const char* IMAGE_VS = R"(#version 300 es
precision highp float;
uniform vec2 uCorners[4];
uniform vec2 uCamera;
uniform float uZoom;
uniform vec2 uScreen;
out vec2 vUv;
void main() {
    vec2 screen = (uCorners[gl_VertexID] - uCamera) * uZoom + uScreen * 0.5;
    gl_Position = vec4(screen.x / uScreen.x * 2.0 - 1.0, 1.0 - screen.y / uScreen.y * 2.0, 0.0, 1.0);
    vUv = vec2(float(gl_VertexID & 1), float(gl_VertexID >> 1));
}
)";

    const char* IMAGE_FS = R"(#version 300 es
precision highp float;
uniform sampler2D uTexture;
uniform float uAlpha;
in vec2 vUv;
out vec4 outColor;
void main() {
    outColor = texture(uTexture, vUv) * uAlpha;
}
)";

    GLuint compile(GLenum type, const char* source) {
        GLuint shader = glCreateShader(type);
        glShaderSource(shader, 1, &source, nullptr);
        glCompileShader(shader);
        GLint ok = 0;
        glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
        if (!ok) {
            char log[1024];
            glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
            std::printf("world shader: %s\n", log);
        }
        return shader;
    }

    GLuint link(const char* vs, const char* fs) {
        GLuint program = glCreateProgram();
        GLuint v = compile(GL_VERTEX_SHADER, vs), f = compile(GL_FRAGMENT_SHADER, fs);
        glAttachShader(program, v);
        glAttachShader(program, f);
        glLinkProgram(program);
        glDeleteShader(v);
        glDeleteShader(f);
        GLint ok = 0;
        glGetProgramiv(program, GL_LINK_STATUS, &ok);
        if (!ok) {
            char log[1024];
            glGetProgramInfoLog(program, sizeof(log), nullptr, log);
            std::printf("world program: %s\n", log);
            glDeleteProgram(program);
            return 0;
        }
        return program;
    }

    // Network colors are 0xRRGGBB; the attribute reads bytes R, G, B, A.
    uint32_t rgba(uint32_t rgb) {
        return ((rgb >> 16) & 0xff) | (rgb & 0xff00) | ((rgb & 0xff) << 16) | 0xff000000u;
    }
}

bool WorldRenderer::init() {
    if (!context) {
        EmscriptenWebGLContextAttributes attrs;
        emscripten_webgl_init_context_attributes(&attrs);
        attrs.alpha = true;
        attrs.depth = false;
        attrs.stencil = false;
        attrs.antialias = false;
        attrs.premultipliedAlpha = true;
        attrs.preserveDrawingBuffer = false;
        attrs.powerPreference = EM_WEBGL_POWER_PREFERENCE_HIGH_PERFORMANCE;
        attrs.majorVersion = 2;
        attrs.minorVersion = 0;
        context = emscripten_webgl_create_context("#world", &attrs);
        if (context <= 0) {
            context = 0;
            return false;
        }
        world_keep_restorable();
    }
    emscripten_webgl_make_context_current(context);

    shapeProgram = link(SHAPE_VS, SHAPE_FS);
    blitProgram = link(BLIT_VS, BLIT_FS);
    patternProgram = link(BLIT_VS, PATTERN_FS);
    imageProgram = link(IMAGE_VS, IMAGE_FS);
    if (!shapeProgram || !blitProgram || !patternProgram || !imageProgram) return false;
    uCamera = glGetUniformLocation(shapeProgram, "uCamera");
    uZoom = glGetUniformLocation(shapeProgram, "uZoom");
    uScreen = glGetUniformLocation(shapeProgram, "uScreen");
    uBlitAlpha = glGetUniformLocation(blitProgram, "uAlpha");
    uBlitTexture = glGetUniformLocation(blitProgram, "uTexture");
    uPatternMap = glGetUniformLocation(patternProgram, "uMap");
    uPatternHeight = glGetUniformLocation(patternProgram, "uHeight");
    uPatternTileSize = glGetUniformLocation(patternProgram, "uTileSize");
    uPatternTexture = glGetUniformLocation(patternProgram, "uTexture");
    uImageCorners = glGetUniformLocation(imageProgram, "uCorners");
    uImageCamera = glGetUniformLocation(imageProgram, "uCamera");
    uImageZoom = glGetUniformLocation(imageProgram, "uZoom");
    uImageScreen = glGetUniformLocation(imageProgram, "uScreen");
    uImageAlpha = glGetUniformLocation(imageProgram, "uAlpha");
    uImageTexture = glGetUniformLocation(imageProgram, "uTexture");

    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);
    glGenBuffers(1, &instanceBuffer);
    glBindBuffer(GL_ARRAY_BUFFER, instanceBuffer);
    GLsizei stride = sizeof(Instance);
    auto attrib = [&](GLuint index, GLint count, GLenum type, bool normalized, size_t offset) {
        glEnableVertexAttribArray(index);
        glVertexAttribPointer(index, count, type, normalized, stride, (const void*)offset);
        glVertexAttribDivisor(index, 1);
    };
    attrib(0, 2, GL_FLOAT, false, offsetof(Instance, x));
    attrib(1, 4, GL_FLOAT, false, offsetof(Instance, size));
    attrib(2, 4, GL_FLOAT, false, offsetof(Instance, kind));
    attrib(3, 4, GL_UNSIGNED_BYTE, true, offsetof(Instance, fill));
    attrib(4, 4, GL_UNSIGNED_BYTE, true, offsetof(Instance, stroke));

    // The blit reads no attributes; it gets a VAO without the instance ones enabled.
    glGenVertexArrays(1, &blitVao);

    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    layers.clear();
    canvasTextures.clear();
    instances.reserve(4096);
    return true;
}

void WorldRenderer::setShown(bool value) {
    if (shown == value) return;
    shown = value;
    world_show(value ? 1 : 0);
}

void WorldRenderer::setBaseCovered(bool value) {
    if (baseCovered == value) return;
    baseCovered = value;
    world_cover_base(value ? 1 : 0);
}

bool WorldRenderer::beginFrame(int frameWidth, int frameHeight, double camX, double camY, double frameZoom) {
    active = false;
    if (failed) return false;
    if (context && emscripten_is_webgl_context_lost(context)) {
        ready = false;
        setShown(false);
        setBaseCovered(false);
        return false;
    }
    if (!ready) {
        if (!init()) {
            failed = true;
            setShown(false);
            setBaseCovered(false);
            return false;
        }
        ready = true;
    }
    emscripten_webgl_make_context_current(context);
    if (frameWidth != width || frameHeight != height) {
        width = frameWidth;
        height = frameHeight;
        emscripten_set_canvas_element_size("#world", width, height);
    }
    cameraX = (float)camX;
    cameraY = (float)camY;
    zoom = (float)frameZoom;
    instances.clear();
    composites.clear();
    coversBase = false;

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, width, height);
    glDisable(GL_SCISSOR_TEST);
    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT);
    glUseProgram(shapeProgram);
    glUniform2f(uCamera, cameraX, cameraY);
    glUniform1f(uZoom, zoom);
    glUniform2f(uScreen, (float)width, (float)height);
    glBindVertexArray(vao);
    setShown(true);
    active = true;
    return true;
}

void WorldRenderer::shape(int sides, uint32_t physicsFlags, uint32_t styleFlags, double size, double shapeWidth,
                          double borderWidth, double x, double y, double angle, uint32_t fill, uint32_t stroke,
                          double alpha) {
    if (!active || sides < 1) return;
    Instance in;
    in.x = (float)x;
    in.y = (float)y;
    in.size = (float)size;
    in.width = (float)shapeWidth;
    in.borderWidth = (float)std::max(0.0, borderWidth);
    in.angle = (float)angle;
    in.count = 0;
    in.param = 0;
    if (sides == 1) {
        in.kind = KindCircle;
    } else if (sides == 2) {
        in.kind = (physicsFlags & PhysicsFlags::isTrapezoid) ? KindTrapezoid : KindRect;
        in.param = 0.875f;
    } else if (styleFlags & StyleFlags::isStar) {
        in.kind = KindStar;
        in.count = (float)sides;
        in.param = 0.4f;
    } else {
        in.kind = KindPolygon;
        in.count = (float)sides;
        in.param = sides == 4 ? (float)(M_PI / 4) : 0.0f;
    }
    in.alpha = (float)alpha;
    in.fill = rgba(fill);
    in.stroke = rgba(stroke);
    instances.push_back(in);
}

void WorldRenderer::solidBackground(uint32_t rgb) {
    if (!active) return;
    flush();
    glClearColor((float)((rgb >> 16) & 0xff) / 255.0f, (float)((rgb >> 8) & 0xff) / 255.0f, (float)(rgb & 0xff) / 255.0f, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glClearColor(0, 0, 0, 0);
    coversBase = true;
}

void WorldRenderer::bindCanvas(int canvas, bool changed, bool repeat) {
    auto it = canvasTextures.find(canvas);
    bool upload = changed || it == canvasTextures.end();
    if (it == canvasTextures.end()) {
        GLuint texture = 0;
        glGenTextures(1, &texture);
        it = canvasTextures.emplace(canvas, texture).first;
    }
    glActiveTexture(GL_TEXTURE0);
    if (upload) {
        // The canvas holds its pixels only once its queued commands have run.
        cp5::flush();
        world_upload_canvas((int)it->second, canvas);
    }
    glBindTexture(GL_TEXTURE_2D, it->second);
    GLint wrap = repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap);
}

void WorldRenderer::patternBackground(cp5::Context& tile, bool changed, double scale, double originX, double originY) {
    if (!active) return;
    flush();
    bindCanvas(tile.handle(), changed, true);
    glUseProgram(patternProgram);
    glBindVertexArray(blitVao);
    glUniform1i(uPatternTexture, 0);
    glUniform3f(uPatternMap, (float)scale, (float)originX, (float)originY);
    glUniform1f(uPatternHeight, (float)height);
    glUniform2f(uPatternTileSize, (float)tile.width(), (float)tile.height());
    glDisable(GL_BLEND);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glEnable(GL_BLEND);
    coversBase = true;
}

void WorldRenderer::screenRect(double x, double y, double w, double h, uint32_t rgb, double alpha) {
    if (!active) return;
    // A rect shape centered on the same spot in world units, sized like fillRect's normalized rect.
    Instance in;
    in.x = (float)((x + w * 0.5 - width * 0.5) / zoom + cameraX);
    in.y = (float)((y + h * 0.5 - height * 0.5) / zoom + cameraY);
    in.size = (float)(std::abs(w) / zoom);
    in.width = (float)(std::abs(h) / zoom);
    in.borderWidth = 0;
    in.angle = 0;
    in.kind = KindRect;
    in.count = 0;
    in.param = 0;
    in.alpha = (float)alpha;
    in.fill = in.stroke = rgba(rgb);
    instances.push_back(in);
}

void WorldRenderer::image(cp5::Context& canvas, bool changed, const double corners[8], double alpha) {
    if (!active) return;
    flush();
    bindCanvas(canvas.handle(), changed, false);
    float c[8];
    for (int i = 0; i < 8; ++i) c[i] = (float)corners[i];
    glUseProgram(imageProgram);
    glBindVertexArray(blitVao);
    glUniform1i(uImageTexture, 0);
    glUniform2fv(uImageCorners, 4, c);
    glUniform2f(uImageCamera, cameraX, cameraY);
    glUniform1f(uImageZoom, zoom);
    glUniform2f(uImageScreen, (float)width, (float)height);
    glUniform1f(uImageAlpha, (float)alpha);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

void WorldRenderer::flush() {
    if (instances.empty()) return;
    glUseProgram(shapeProgram);
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, instanceBuffer);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(instances.size() * sizeof(Instance)), instances.data(), GL_STREAM_DRAW);
    glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, (GLsizei)instances.size());
    instances.clear();
}

// depth 0 is the canvas; depth n draws into layers[n - 1], sized to the canvas.
void WorldRenderer::bindTarget(size_t depth) {
    if (depth == 0) {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        return;
    }
    if (layers.size() < depth) layers.resize(depth);
    Layer& layer = layers[depth - 1];
    if (!layer.fbo || layer.width != width || layer.height != height) {
        if (!layer.fbo) {
            glGenFramebuffers(1, &layer.fbo);
            glGenTextures(1, &layer.texture);
        }
        glBindTexture(GL_TEXTURE_2D, layer.texture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindFramebuffer(GL_FRAMEBUFFER, layer.fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, layer.texture, 0);
        layer.width = width;
        layer.height = height;
        return;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, layer.fbo);
}

void WorldRenderer::scissorTo(const Rect& r) {
    glScissor(r.x0, height - r.y1, std::max(0, r.x1 - r.x0), std::max(0, r.y1 - r.y0));
}

void WorldRenderer::beginComposite(double alpha, double x, double y, double w, double h) {
    if (!active) return;
    flush();
    Rect r{(int)std::floor(x), (int)std::floor(y), (int)std::ceil(x + w), (int)std::ceil(y + h)};
    Rect parent = composites.empty() ? Rect{0, 0, width, height} : composites.back().rect;
    r = {std::max(r.x0, parent.x0), std::max(r.y0, parent.y0), std::min(r.x1, parent.x1), std::min(r.y1, parent.y1)};
    composites.push_back({(float)alpha, r});
    bindTarget(composites.size());
    glEnable(GL_SCISSOR_TEST);
    scissorTo(r);
    glClear(GL_COLOR_BUFFER_BIT);
}

void WorldRenderer::endComposite() {
    if (!active || composites.empty()) return;
    flush();
    Composite done = composites.back();
    composites.pop_back();
    size_t depth = composites.size();
    bindTarget(depth);
    scissorTo(done.rect);
    glUseProgram(blitProgram);
    glBindVertexArray(blitVao);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, layers[depth].texture);
    glUniform1i(uBlitTexture, 0);
    glUniform1f(uBlitAlpha, done.alpha);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    if (composites.empty()) glDisable(GL_SCISSOR_TEST);
    else scissorTo(composites.back().rect);
}

void WorldRenderer::endFrame() {
    if (!active) return;
    while (!composites.empty()) endComposite();
    flush();
    setBaseCovered(coversBase);
    active = false;
}

void WorldRenderer::disable() {
    active = false;
    setShown(false);
    setBaseCovered(false);
}
