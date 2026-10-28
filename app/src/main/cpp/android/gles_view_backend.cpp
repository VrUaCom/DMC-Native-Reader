// OpenGL ES 3 backend of the view renderer (view_gpu.h).
//
// One offscreen EGL context (pbuffer 1x1, the picture goes to a framebuffer
// object) shared by every thread that renders: draw() takes the lock, makes
// the context current, draws the frame Core prepared, resolves the MSAA
// target, reads the pixels back and releases the context again. Textures and
// the room geometry stay on the GPU between frames; the posed model is
// uploaded every frame. Nothing here decides what a triangle looks like —
// those rules come from Core's GpuViewFrame — the shaders only evaluate them
// per pixel.
#include "gles_view_backend.h"
#include "dmcresource/frame_profile.h"

#include <EGL/egl.h>
#ifdef __ANDROID__
#include <android/native_window.h>
#endif
#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#include <GLES2/gl2ext.h>

#include <algorithm>
#include <iterator>
#include <array>
#include <memory>
#include <cstddef>
#include <cstring>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "dmcresource/view_gpu.h"

namespace dmcviewer {
namespace {

using dmcresource::GpuBatch;
using dmcresource::GpuBatchKind;
using dmcresource::GpuScreenVertex;
using dmcresource::GpuVertex;
using dmcresource::GpuViewFrame;
namespace frame_profile = dmcresource::frame_profile;
using dmcresource::ImagePreview;
using dmcresource::RgbaImage;

constexpr const char* kMeshVertex = R"(#version 300 es
layout(location = 0) in vec3 a_pos;
layout(location = 1) in vec2 a_uv;
layout(location = 2) in vec4 a_color;
layout(location = 3) in vec3 a_normal;
layout(location = 4) in float a_light;
uniform mat4 u_view;
uniform vec4 u_proj;
uniform float u_cd;
uniform int u_room;
uniform vec2 u_light;
uniform vec2 u_scroll;
uniform float u_depth_bias;
out vec2 v_uv;
out vec4 v_color;
out float v_light;
out float v_z;
void main() {
    vec3 c = (u_view * vec4(a_pos, 1.0)).xyz;
    float term = a_light;
    if (u_room == 1) {
        vec3 n = mat3(u_view) * a_normal;
        float nl = length(n);
        float pl = length(c);
        term = (nl > 0.0 && pl > 0.0) ? abs(dot(n, c)) / (nl * pl) : 1.0;
    }
    v_light = u_light.x + u_light.y * term;
    v_uv = a_uv + u_scroll;
    v_color = a_color * (255.0 / 128.0);
    v_z = c.z - u_cd;
    gl_Position = vec4(u_proj.x * c.x, u_proj.y * c.y, u_proj.z * c.z + u_proj.w, c.z);
    if (u_depth_bias != 0.0) {
        float zb = max(c.z - u_depth_bias, 1.0e-4);
        gl_Position.z = (u_proj.z + u_proj.w / zb) * c.z;
    }
}
)";

// u_mode: 0 model, 1 model additive / subtractive, 2 room opaque texels,
// 3 room soft texels, 4 room additive / subtractive, 5 solid colour, 6 shadow.
constexpr const char* kMeshFragment = R"(#version 300 es
precision highp float;
in vec2 v_uv;
in vec4 v_color;
in float v_light;
in float v_z;
uniform sampler2D u_tex;
uniform int u_textured;
uniform int u_mode;
uniform int u_colored;
uniform int u_translucent;
uniform float u_radius;
uniform vec4 u_solid;
uniform float u_cd;
uniform vec4 u_fog;       // x on, y start, z end (camera distance)
uniform vec4 u_fog_near;  // rgb, alpha at start
uniform vec4 u_fog_far;   // rgb, alpha at end
out vec4 o;
// The depth fog's colour (rgb) and weight (a) at this surface.
vec4 fog_at() {
    if (u_fog.x < 0.5) return vec4(0.0);
    float z = v_z + u_cd;
    float span = u_fog.z - u_fog.y;
    float t = span > 1.0e-3 ? clamp((z - u_fog.y) / span, 0.0, 1.0) : (z >= u_fog.z ? 1.0 : 0.0);
    return vec4(mix(u_fog_near.rgb, u_fog_far.rgb, t), clamp(mix(u_fog_near.a, u_fog_far.a, t), 0.0, 1.0));
}
void main() {
    vec4 fog = fog_at();
    if (u_mode == 5) { o = u_solid; return; }
    if (u_mode == 6) { o = vec4(0.45, 0.45, 0.45, 1.0); return; }
    if (u_mode <= 1) {
        if (u_textured == 0) {
            float zn = 0.5 + 0.5 * tanh(-v_z / u_radius);
            float s = floor(145.0 + 80.0 * zn) / 255.0;
            o = vec4(s, s, min(1.0, s + 10.0 / 255.0), 1.0);
            return;
        }
        vec4 t = texture(u_tex, v_uv);
        if (u_colored == 1) t = min(t * v_color, vec4(1.0));
        t.rgb = min(t.rgb * v_light, vec3(1.0));
        if (t.a < 0.5 / 255.0) discard;
        // An added (mode 1) surface fades under the fog, a drawn one takes its colour.
        t.rgb = u_mode == 1 ? t.rgb * (1.0 - fog.a) : mix(t.rgb, fog.rgb, fog.a);
        o = t;
        return;
    }
    vec4 t = u_textured == 1 ? texture(u_tex, v_uv) : vec4(128.0 / 255.0, 128.0 / 255.0, 128.0 / 255.0, 1.0);
    if (t.a < 7.5 / 255.0) discard;
    bool opaque = u_mode == 2 || u_mode == 3 ? (t.a >= 239.5 / 255.0 || u_translucent == 0) : false;
    if (u_mode == 2) {
        if (!opaque) discard;
        if (u_translucent == 0 && t.a < 31.5 / 255.0) discard;
    } else if (opaque) {
        discard;
    }
    vec3 c = t.rgb;
    if (u_colored == 1) c *= v_color.rgb;
    c *= v_light;
    if (u_mode == 4) {
        float k = t.a;
        if (u_colored == 1) k *= min(1.0, v_color.a);
        o = vec4(c * k * (1.0 - fog.a), 1.0);
        return;
    }
    o = vec4(mix(min(c, vec3(1.0)), fog.rgb, fog.a), u_mode == 3 ? t.a : 1.0);
}
)";

// Effect quads: image-space positions (affine, like the software pass).
constexpr const char* kScreenVertex = R"(#version 300 es
layout(location = 0) in vec3 a_pos;
layout(location = 1) in vec2 a_uv;
layout(location = 2) in vec4 a_color;
uniform vec2 u_size;
uniform vec3 u_depth;
out vec2 v_uv;
out vec4 v_color;
void main() {
    float cz = a_pos.z + u_depth.z;
    gl_Position = vec4(a_pos.x * 2.0 / u_size.x - 1.0, a_pos.y * 2.0 / u_size.y - 1.0,
                       u_depth.x + u_depth.y / cz, 1.0);
    v_uv = a_uv;
    v_color = a_color / 255.0;
}
)";

constexpr const char* kScreenFragment = R"(#version 300 es
precision highp float;
in vec2 v_uv;
in vec4 v_color;
uniform sampler2D u_tex;
uniform int u_textured;
out vec4 o;
void main() {
    vec4 t = u_textured == 1 ? texture(u_tex, v_uv) : vec4(1.0);
    bool tinted = any(lessThan(v_color, vec4(254.5 / 255.0)));
    if (tinted) t *= v_color;
    if (t.a < (tinted ? 1.5 : 7.5) / 255.0) discard;
    o = t;
}
)";

// ---- The photo camera's development (CameraPost) -----------------------------
// Full-screen passes over the drawn frame: the depth of field (from the
// frame's own depth), then the lens, polarizer and look, then sharpening.
// The numbers are the core's (spider/camera.cpp); these only evaluate them.

constexpr const char* kFullVertex = R"(#version 300 es
void main() {
    vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
)";

// Camera distance from the depth buffer: clip z = p.x * c.z + p.y, w = c.z.
constexpr const char* kDepthHelpers = R"(
uniform highp sampler2D u_depth;
uniform vec2 u_proj;
// Scene units; < 0 where nothing was drawn (infinity).
float distance_at(ivec2 p) {
    float d = texelFetch(u_depth, p, 0).r;
    if (d >= 0.999999) return -1.0;
    return u_proj.y / ((2.0 * d - 1.0) - u_proj.x);
}
)";

// The auto focus: a 16 x 16 grid over the middle of the frame, distances
// packed into RGBA8 (units x 16 as a 32-bit integer) for the CPU's median.
constexpr const char* kFocusFragment = R"(#version 300 es
precision highp float;
precision highp int;
%DEPTH%
uniform ivec2 u_size;
uniform float u_half;
out vec4 o;
void main() {
    vec2 cell = floor(gl_FragCoord.xy);
    vec2 p = vec2(u_size) * 0.5 + (cell - 7.5) / 7.5 * u_half;
    float z = distance_at(ivec2(clamp(p, vec2(0.0), vec2(u_size) - 1.0)));
    uint v = z < 0.0 ? 0u : uint(clamp(z * 16.0, 1.0, 4.0e9));
    o = vec4(float(v & 255u), float((v >> 8) & 255u), float((v >> 16) & 255u), float(v >> 24)) / 255.0;
}
)";

constexpr const char* kDofFragment = R"(#version 300 es
precision highp float;
precision highp int;
%DEPTH%
uniform sampler2D u_color;
uniform ivec2 u_size;
uniform float u_focus_mm;  // > 0 that distance, < 0 infinity
uniform float u_f;
uniform float u_k;
uniform float u_unit;
uniform float u_px_per_mm;
uniform float u_max_r;
uniform int u_taps;
uniform int u_bright;
uniform vec2 u_tap[128];
out vec4 o;
float to_mm(float z) { return z < 0.0 ? -1.0 : max(z * u_unit, u_f * 1.05); }
float radius(float zmm) {
    float coc;
    if (u_focus_mm > 0.0) {
        float s = max(u_focus_mm, u_f * 1.05);
        coc = zmm < 0.0 ? u_k / (s - u_f) : u_k / (s - u_f) * abs(zmm - s) / zmm;
    } else {
        coc = zmm < 0.0 ? 0.0 : u_k / zmm;
    }
    return min(u_max_r, 0.5 * coc * u_px_per_mm);
}
void main() {
    ivec2 p = ivec2(gl_FragCoord.xy);
    vec4 c0 = texelFetch(u_color, p, 0);
    float zp = to_mm(distance_at(p));
    float r = radius(zp);
    if (r < 0.5) { o = c0; return; }
    int n = clamp(int(r * r * 1.5) + 4, 4, u_taps);
    vec3 sum = vec3(0.0);
    float weights = 0.0;
    for (int t = 0; t < 128; ++t) {
        if (t >= n) break;
        vec2 d = u_tap[t] * r;
        ivec2 q = clamp(p + ivec2(round(d)), ivec2(0), u_size - 1);
        float zq = to_mm(distance_at(q));
        float rq = radius(zq);
        // A nearer pixel reaches over this one; a farther one no further than this one's own circle.
        bool nearer = zq >= 0.0 && (zp < 0.0 || zq < zp);
        float reach = nearer ? rq : min(rq, r);
        if (reach + 0.5 < length(d)) continue;
        vec3 c = texelFetch(u_color, q, 0).rgb;
        float w = 1.0;
        if (u_bright == 1) w += 8.0 * max(0.0, dot(c, vec3(0.2126, 0.7152, 0.0722)) - 0.75);
        sum += w * c;
        weights += w;
    }
    o = vec4(weights > 0.0 ? sum / weights : c0.rgb, 1.0);
}
)";

constexpr const char* kLookFragment = R"(#version 300 es
precision highp float;
precision highp int;
uniform sampler2D u_src;
uniform sampler2D u_curves;  // 256 x 1: r, g, b curves
uniform ivec2 u_size;
uniform float u_k;
uniform float u_fringe;
uniform int u_pol;
uniform float u_pol_strength;
uniform float u_ring;
uniform float u_sat;
uniform vec3 u_tint;
uniform int u_cinema;
uniform float u_vignette;
uniform float u_sigma;
uniform int u_grain;
uniform int u_chroma;
uniform uint u_seed;
out vec4 o;
float s2l(float v) { return v <= 0.04045 ? v / 12.92 : pow((v + 0.055) / 1.055, 2.4); }
float l2s(float v) { v = clamp(v, 0.0, 1.0); return v <= 0.0031308 ? v * 12.92 : 1.055 * pow(v, 1.0 / 2.4) - 0.055; }
uint hash(uint x, uint y, uint seed) {
    uint h = x * 0x8DA6B343u ^ y * 0xD8163841u ^ seed * 0xCB1AB31Fu;
    h ^= h >> 13u;
    h *= 0x5BD1E995u;
    h ^= h >> 15u;
    return h;
}
float noise(uint h) { return (float(h & 0xFFFFu) + float(h >> 16u)) / 65535.0 - 1.0; }
vec3 at(vec2 pixel) { return texture(u_src, (pixel + 0.5) / vec2(u_size)).rgb; }
void main() {
    vec2 xy = floor(gl_FragCoord.xy);
    vec2 size = vec2(u_size);
    vec2 c = size * 0.5;
    float hd = length(c);
    vec2 d = (xy + 0.5 - c) / hd;
    vec3 col;
    if (u_k == 0.0 && u_fringe == 0.0) {
        col = texelFetch(u_src, ivec2(xy), 0).rgb;
    } else {
        // Distortion (the corners stay the corners) and fringing, radial.
        float scale = (1.0 + u_k * dot(d, d)) / (1.0 + u_k);
        col.g = at(c + d * scale * hd - 0.5).g;
        col.r = at(c + d * scale * (1.0 + u_fringe) * hd - 0.5).r;
        col.b = at(c + d * scale * (1.0 - u_fringe) * hd - 0.5).b;
    }
    if (u_pol == 1) {
        float hi = max(col.r, max(col.g, col.b));
        float lo = min(col.r, min(col.g, col.b));
        float saturation = hi > 1.0e-3 ? (hi - lo) / hi : 0.0;
        float luma = dot(col, vec3(0.2126, 0.7152, 0.0722));
        float glare = smoothstep(0.55, 0.95, luma) * (1.0 - saturation);
        float sky = clamp((col.b - max(col.r, col.g)) / max(col.b, 1.0e-3), 0.0, 1.0) * smoothstep(0.25, 0.7, col.b);
        float polarized = clamp(0.15 + 0.85 * glare + 0.6 * sky, 0.0, 0.95);
        float light = 0.5 * (xy.x / size.x - 0.5) + 0.3 * (xy.y / size.y - 0.5);
        float s = sin(u_ring - light);
        float held = u_pol_strength * polarized * s * s;
        vec3 lin = vec3(s2l(col.r), s2l(col.g), s2l(col.b)) * (1.0 - held);
        float l = dot(lin, vec3(0.2126, 0.7152, 0.0722));
        lin = l + (lin - l) * (1.0 + 0.8 * held);
        col = vec3(l2s(lin.r), l2s(lin.g), l2s(lin.b));
    }
    ivec3 i = ivec3(clamp(col * 255.0 + 0.5, 0.0, 255.0));
    vec3 v = vec3(texelFetch(u_curves, ivec2(i.r, 0), 0).r, texelFetch(u_curves, ivec2(i.g, 0), 0).g,
                  texelFetch(u_curves, ivec2(i.b, 0), 0).b) * 255.0;
    float luma = dot(v, vec3(0.2126, 0.7152, 0.0722));
    if (u_sat != 0.0) v = luma + (v - luma) * (1.0 + u_sat);
    if (u_cinema == 1) {
        float t = luma / 255.0;
        v.r *= 0.92 + 0.16 * t;
        v.b *= 1.08 - 0.18 * t;
    } else {
        v *= u_tint;
    }
    if (u_vignette > 0.0) {
        vec2 off = xy - c;
        float rr = dot(off, off) / dot(c, c);
        v *= 1.0 - u_vignette * rr * sqrt(rr);
    }
    if (u_sigma > 0.0) {
        float shade = 1.3 - 0.7 * clamp(luma / 255.0, 0.0, 1.0);
        uint gx = uint(xy.x) / uint(u_grain);
        uint gy = uint(xy.y) / uint(u_grain);
        v += noise(hash(gx, gy, u_seed)) * u_sigma * shade;
        if (u_chroma == 1) {
            v.r += noise(hash(gx, gy, u_seed ^ 0x9E3779B9u)) * u_sigma * 0.6 * shade;
            v.b += noise(hash(gx, gy, u_seed ^ 0x7F4A7C15u)) * u_sigma * 0.6 * shade;
        }
    }
    o = vec4(clamp((v + 0.5) / 255.0, 0.0, 1.0), 1.0);
}
)";

constexpr const char* kSharpenFragment = R"(#version 300 es
precision highp float;
uniform sampler2D u_src;
uniform ivec2 u_size;
uniform float u_amount;
out vec4 o;
void main() {
    ivec2 p = ivec2(gl_FragCoord.xy);
    vec3 c = texelFetch(u_src, p, 0).rgb;
    if (p.x == 0 || p.y == 0 || p.x == u_size.x - 1 || p.y == u_size.y - 1) { o = vec4(c, 1.0); return; }
    vec3 around = texelFetch(u_src, p + ivec2(1, 0), 0).rgb + texelFetch(u_src, p - ivec2(1, 0), 0).rgb +
                  texelFetch(u_src, p + ivec2(0, 1), 0).rgb + texelFetch(u_src, p - ivec2(0, 1), 0).rgb;
    o = vec4(clamp(c + u_amount * (c - around * 0.25), 0.0, 1.0), 1.0);
}
)";

std::string with_depth(const char* source) {
    std::string text = source;
    const auto at = text.find("%DEPTH%");
    if (at != std::string::npos) text.replace(at, 7, kDepthHelpers);
    return text;
}

GLuint compile(GLenum type, const char* source) {
    const GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    GLint ok = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (ok != GL_TRUE) {
        glDeleteShader(shader);
        return 0U;
    }
    return shader;
}

// A shader object, or 0 with the driver's log in `log`.
GLuint compile_logged(GLenum type, const char* source, std::string& log) {
    const GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    GLint ok = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (ok == GL_TRUE) return shader;
    GLint length = 0;
    glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &length);
    log.assign(static_cast<std::size_t>(std::max(length, 1)), '\0');
    glGetShaderInfoLog(shader, length, nullptr, log.data());
    while (!log.empty() && (log.back() == '\0' || log.back() == '\n')) log.pop_back();
    glDeleteShader(shader);
    return 0U;
}

// camera = view * p; clip = (p0 x, p1 y, p2 z + p3, z) as one matrix
// (column-major), for the DMC3 shaders' u_view_projection.
std::array<float, 16> view_projection(const std::array<float, 16>& view, const std::array<float, 4>& p) noexcept {
    std::array<float, 16> m{};
    for (std::size_t c = 0U; c < 4U; ++c) {
        m[c * 4U + 0U] = p[0] * view[c * 4U + 0U];
        m[c * 4U + 1U] = p[1] * view[c * 4U + 1U];
        m[c * 4U + 2U] = p[2] * view[c * 4U + 2U] + p[3] * view[c * 4U + 3U];
        m[c * 4U + 3U] = view[c * 4U + 2U];
    }
    return m;
}

GLuint link(const char* vertex, const char* fragment) {
    const GLuint vs = compile(GL_VERTEX_SHADER, vertex);
    const GLuint fs = compile(GL_FRAGMENT_SHADER, fragment);
    if (vs == 0U || fs == 0U) {
        if (vs != 0U) glDeleteShader(vs);
        if (fs != 0U) glDeleteShader(fs);
        return 0U;
    }
    const GLuint program = glCreateProgram();
    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glLinkProgram(program);
    glDeleteShader(vs);
    glDeleteShader(fs);
    GLint ok = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (ok != GL_TRUE) {
        glDeleteProgram(program);
        return 0U;
    }
    return program;
}

// FNV-1a over a few texels: a texture at a reused address is re-uploaded.
std::uint64_t texel_signature(const ImagePreview& image) noexcept {
    std::uint64_t h = 0xCBF29CE484222325ULL;
    const auto n = image.rgba8.size();
    for (std::size_t k = 0U; k < 32U && n != 0U; ++k) {
        h ^= image.rgba8[k * (n - 1U) / 31U];
        h *= 0x100000001B3ULL;
    }
    return h ^ (static_cast<std::uint64_t>(image.width) << 32U) ^ image.height;
}

class GlesViewBackend final : public dmcresource::GpuViewBackend {
public:
    bool draw(const GpuViewFrame& frame, RgbaImage& image) override {
        const std::lock_guard lock{mutex_};
        if (!initialize() || !make_current()) return false;
        const bool ok = draw_current(frame, image);
        release_current();
        if (!ok && ++failures_ >= 8U) failed_ = true;  // stop retrying a broken driver
        if (ok) failures_ = 0U;
        return ok;
    }

    std::string describe() override {
        const std::lock_guard lock{mutex_};
        if (!initialize()) return {};
        return description_;
    }

    dmcresource::GpuCapabilities capabilities() override {
        const std::lock_guard lock{mutex_};
        if (!initialize()) return {};
        return {static_cast<int>(max_samples_), max_anisotropy_};
    }

    void set_window(void* window) override {
        const std::lock_guard lock{mutex_};
        drop_window();
        window_ = window;
        window_failed_ = false;
    }

private:
    struct Texture final {
        GLuint id{};
        const void* data{};
        std::uint64_t signature{};
        std::uint64_t last_frame{};
        std::size_t bytes{};
    };

    struct Uniforms final {
        GLint view{}, proj{}, cd{}, room{}, light{}, scroll{}, depth_bias{};
        GLint tex{}, textured{}, mode{}, colored{}, translucent{}, radius{}, solid{};
        GLint fog{}, fog_near{}, fog_far{};
    };

    bool initialize() {
        if (failed_) return false;
        if (context_ != EGL_NO_CONTEXT) return true;
        display_ = eglGetDisplay(EGL_DEFAULT_DISPLAY);
        if (display_ == EGL_NO_DISPLAY || eglInitialize(display_, nullptr, nullptr) != EGL_TRUE) {
            failed_ = true;
            return false;
        }
        eglBindAPI(EGL_OPENGL_ES_API);
        const EGLint config_attributes[] = {
            EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT_KHR,
            EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
            EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
            EGL_NONE,
        };
        EGLConfig config = nullptr;
        EGLint count = 0;
        // A configuration that also draws into windows, so frames can be
        // shown straight on the view; else offscreen only.
        EGLint window_attributes[std::size(config_attributes)];
        std::copy(std::begin(config_attributes), std::end(config_attributes), window_attributes);
        window_attributes[3] = EGL_PBUFFER_BIT | EGL_WINDOW_BIT;
        if (eglChooseConfig(display_, window_attributes, &config, 1, &count) == EGL_TRUE && count >= 1) {
            window_config_ = true;
        } else if (eglChooseConfig(display_, config_attributes, &config, 1, &count) != EGL_TRUE || count < 1) {
            failed_ = true;
            return false;
        }
        config_ = config;
#ifndef __ANDROID__
        window_config_ = true;  // the desktop stand-in is a pbuffer
#endif
        const EGLint context_attributes[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
        context_ = eglCreateContext(display_, config, EGL_NO_CONTEXT, context_attributes);
        const EGLint surface_attributes[] = {EGL_WIDTH, 1, EGL_HEIGHT, 1, EGL_NONE};
        surface_ = eglCreatePbufferSurface(display_, config, surface_attributes);
        if (context_ == EGL_NO_CONTEXT || surface_ == EGL_NO_SURFACE || !make_current()) {
            failed_ = true;
            return false;
        }
        const bool ready = create_resources();
        release_current();
        if (!ready) failed_ = true;
        return ready;
    }

    bool make_current() {
        return eglMakeCurrent(display_, surface_, surface_, context_) == EGL_TRUE;
    }

    void release_current() {
        eglMakeCurrent(display_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    }

    bool create_resources() {
        const auto text = [](GLenum name) {
            const auto* value = reinterpret_cast<const char*>(glGetString(name));
            return std::string(value != nullptr ? value : "");
        };
        auto version = text(GL_VERSION);
        // "OpenGL ES 3.2 V@0800.0 ..." -> "OpenGL ES 3.2"
        if (version.size() > 13U) version.resize(13U);
        description_ = version + " / " + text(GL_RENDERER);

        mesh_program_ = link(kMeshVertex, kMeshFragment);
        screen_program_ = link(kScreenVertex, kScreenFragment);
        if (mesh_program_ == 0U || screen_program_ == 0U) return false;
        const auto at = [this](const char* name) { return glGetUniformLocation(mesh_program_, name); };
        u_.view = at("u_view");
        u_.proj = at("u_proj");
        u_.cd = at("u_cd");
        u_.room = at("u_room");
        u_.light = at("u_light");
        u_.scroll = at("u_scroll");
        u_.depth_bias = at("u_depth_bias");
        u_.tex = at("u_tex");
        u_.textured = at("u_textured");
        u_.mode = at("u_mode");
        u_.colored = at("u_colored");
        u_.translucent = at("u_translucent");
        u_.radius = at("u_radius");
        u_.fog = at("u_fog");
        u_.fog_near = at("u_fog_near");
        u_.fog_far = at("u_fog_far");
        u_.solid = at("u_solid");
        screen_size_ = glGetUniformLocation(screen_program_, "u_size");
        screen_depth_ = glGetUniformLocation(screen_program_, "u_depth");
        screen_tex_ = glGetUniformLocation(screen_program_, "u_tex");
        screen_textured_ = glGetUniformLocation(screen_program_, "u_textured");

        glGenBuffers(1, &model_vbo_);
        glGenBuffers(1, &room_vbo_);
        glGenBuffers(1, &aux_vbo_);
        glGenBuffers(1, &screen_vbo_);
        glGenVertexArrays(1, &model_vao_);
        glGenVertexArrays(1, &room_vao_);
        glGenVertexArrays(1, &aux_vao_);
        glGenVertexArrays(1, &screen_vao_);
        mesh_layout(model_vao_, model_vbo_);
        mesh_layout(room_vao_, room_vbo_);
        mesh_layout(aux_vao_, aux_vbo_);
        glGenVertexArrays(1, &model_dmc3_vao_);
        glGenVertexArrays(1, &room_dmc3_vao_);
        dmc3_layout(model_dmc3_vao_, model_vbo_, true);
        dmc3_layout(room_dmc3_vao_, room_vbo_, false);
        compile_dmc3_shaders();
        glBindVertexArray(screen_vao_);
        glBindBuffer(GL_ARRAY_BUFFER, screen_vbo_);
        constexpr auto stride = static_cast<GLsizei>(sizeof(GpuScreenVertex));
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride,
                              reinterpret_cast<const void*>(offsetof(GpuScreenVertex, x)));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride,
                              reinterpret_cast<const void*>(offsetof(GpuScreenVertex, u)));
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, stride,
                              reinterpret_cast<const void*>(offsetof(GpuScreenVertex, rgba)));
        glBindVertexArray(0);

        // Pixel (original) look and the smooth one; filters follow the
        // Graphics settings (apply_sampler_options).
        glGenSamplers(1, &nearest_);
        glSamplerParameteri(nearest_, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glSamplerParameteri(nearest_, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glSamplerParameteri(nearest_, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glGenSamplers(1, &smooth_);
        glSamplerParameteri(smooth_, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glSamplerParameteri(smooth_, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glSamplerParameteri(smooth_, GL_TEXTURE_WRAP_T, GL_REPEAT);
        const std::string extensions = text(GL_EXTENSIONS);
        if (extensions.find("GL_EXT_texture_filter_anisotropic") != std::string::npos) {
            GLfloat most = 1.0F;
            glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &most);
            max_anisotropy_ = most;
        }
        glGetIntegerv(GL_MAX_SAMPLES, &max_samples_);
        glGetIntegerv(GL_MAX_RENDERBUFFER_SIZE, &max_target_);
        glGetIntegerv(GL_MAX_TEXTURE_SIZE, &max_texture_);
        return glGetError() == GL_NO_ERROR;
    }

    static void mesh_layout(GLuint vao, GLuint vbo) {
        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        constexpr auto stride = static_cast<GLsizei>(sizeof(GpuVertex));
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<const void*>(offsetof(GpuVertex, x)));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<const void*>(offsetof(GpuVertex, u)));
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, stride,
                              reinterpret_cast<const void*>(offsetof(GpuVertex, rgba)));
        glEnableVertexAttribArray(3);
        glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<const void*>(offsetof(GpuVertex, nx)));
        glEnableVertexAttribArray(4);
        glVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, stride,
                              reinterpret_cast<const void*>(offsetof(GpuVertex, light)));
        glBindVertexArray(0);
    }

    // The DMC3 shaders' inputs (CONVENTIONS.md) on the same vertices:
    // 0 position, 1 normal, 2 uv, 3 colour (0..1 for the models, 0..255 for
    // the stage); the bones and weights are constants (dmc3_constants).
    static void dmc3_layout(GLuint vao, GLuint vbo, bool unit_colour) {
        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        constexpr auto stride = static_cast<GLsizei>(sizeof(GpuVertex));
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<const void*>(offsetof(GpuVertex, x)));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<const void*>(offsetof(GpuVertex, nx)));
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<const void*>(offsetof(GpuVertex, u)));
        glEnableVertexAttribArray(3);
        glVertexAttribPointer(3, 4, GL_UNSIGNED_BYTE, unit_colour ? GL_TRUE : GL_FALSE, stride,
                              reinterpret_cast<const void*>(offsetof(GpuVertex, rgba)));
        glBindVertexArray(0);
    }

    // The vertices come posed: every one follows bone 0 (rows 0..3, the
    // identity) alone; no per-vertex fog.
    static void dmc3_constants() {
        glVertexAttribI4i(4, 0, 0, 0, 0);
        glVertexAttrib4f(5, 31.0F, 0.0F, 0.0F, 1.0F);
        glVertexAttrib4f(6, 1.0F, 0.0F, 0.0F, 1.0F);
    }

    // All of them once, when the context is made: the count goes to the
    // Info screen, and a pair is linked when a frame first needs it.
    void compile_dmc3_shaders() {
        dmcresource::GpuShaderStats stats;
        for (const auto& entry : dmcresource::dmc3::shaders()) {
            const auto text = dmcresource::dmc3::source(entry.id);
            std::string log = text.empty() ? "no source in this build" : "";
            GLuint shader = 0U;
            if (!text.empty()) {
                const std::string source(text);
                shader = compile_logged(entry.stage == dmcresource::dmc3::ShaderStage::vertex ? GL_VERTEX_SHADER
                                                                                             : GL_FRAGMENT_SHADER,
                                        source.c_str(), log);
            }
            dmc3_objects_[static_cast<std::size_t>(entry.id)] = shader;
            if (shader != 0U) {
                ++stats.compiled;
            } else {
                ++stats.failed;
                if (stats.first_failure.empty()) stats.first_failure = std::string(entry.name) + ": " + log;
            }
        }
        dmc3_compiled_ = stats.compiled;
        dmc3_failed_ = stats.failed;
        dmc3_first_failure_ = stats.first_failure;
        dmcresource::set_gpu_shader_stats(std::move(stats));
    }

    // Uniform locations of a DMC3 pair (-1 where the pair has none).
    struct Dmc3Program final {
        GLuint id{};
        GLint view_projection{}, world_view_projection{}, screen_scale{}, screen_offset{}, bone_rows{};
        GLint light_directions{}, light_colours{}, part_colour{}, part_alpha{}, uv_scroll{}, uv_scale{};
        GLint fog_colour{}, fog_coeffs{}, light_x{}, light_y{}, light_z{}, light_falloff{}, light_colour{};
        GLint ambient{}, texture{}, alpha_test{}, alpha_func{}, alpha_ref{};
    };

    const Dmc3Program* dmc3_program(dmcresource::dmc3::Shader vertex, dmcresource::dmc3::Shader fragment) {
        const auto key = static_cast<std::uint32_t>(vertex) << 8U | static_cast<std::uint32_t>(fragment);
        if (const auto it = dmc3_programs_.find(key); it != dmc3_programs_.end()) {
            return it->second.id != 0U ? &it->second : nullptr;
        }
        Dmc3Program program;
        const auto vs = static_cast<std::size_t>(vertex);
        const auto fs = static_cast<std::size_t>(fragment);
        if (vs < dmc3_objects_.size() && fs < dmc3_objects_.size() && dmc3_objects_[vs] != 0U &&
            dmc3_objects_[fs] != 0U) {
            const GLuint id = glCreateProgram();
            glAttachShader(id, dmc3_objects_[vs]);
            glAttachShader(id, dmc3_objects_[fs]);
            glLinkProgram(id);
            glDetachShader(id, dmc3_objects_[vs]);
            glDetachShader(id, dmc3_objects_[fs]);
            GLint ok = GL_FALSE;
            glGetProgramiv(id, GL_LINK_STATUS, &ok);
            if (ok == GL_TRUE) {
                program.id = id;
            } else {
                glDeleteProgram(id);
            }
        }
        if (program.id != 0U) {
            const auto at = [&program](const char* name) { return glGetUniformLocation(program.id, name); };
            program.view_projection = at("u_view_projection");
            program.world_view_projection = at("u_world_view_projection");
            program.screen_scale = at("u_screen_scale");
            program.screen_offset = at("u_screen_offset");
            program.bone_rows = at("u_bone_rows");
            program.light_directions = at("u_light_directions");
            program.light_colours = at("u_light_colours");
            program.part_colour = at("u_part_colour");
            program.part_alpha = at("u_part_alpha");
            program.uv_scroll = at("u_uv_scroll");
            program.uv_scale = at("u_uv_scale");
            program.fog_colour = at("u_fog_colour");
            program.fog_coeffs = at("u_fog_coeffs");
            program.light_x = at("u_light_x");
            program.light_y = at("u_light_y");
            program.light_z = at("u_light_z");
            program.light_falloff = at("u_light_falloff");
            program.light_colour = at("u_light_colour");
            program.ambient = at("u_ambient");
            program.texture = at("u_texture");
            program.alpha_test = at("u_alpha_test");
            program.alpha_func = at("u_alpha_func");
            program.alpha_ref = at("u_alpha_ref");
            // What never changes: bone 0 the identity, no screen mapping,
            // the Reader's 0..1 coordinates (x 4096 undoes the fixed point),
            // no point lights, texture unit 0.
            glUseProgram(program.id);
            static constexpr float kIdentityRows[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
            glUniform4fv(program.bone_rows, 4, kIdentityRows);
            glUniform4f(program.screen_scale, 1.0F, 1.0F, 1.0F, 1.0F);
            glUniform4f(program.screen_offset, 0.0F, 0.0F, 0.0F, 0.0F);
            glUniform2f(program.uv_scale, 4096.0F, 4096.0F);
            glUniform4f(program.light_x, 0.0F, 0.0F, 0.0F, 0.0F);
            glUniform4f(program.light_y, 0.0F, 0.0F, 0.0F, 0.0F);
            glUniform4f(program.light_z, 0.0F, 0.0F, 0.0F, 0.0F);
            glUniform4f(program.light_falloff, 0.0F, 0.0F, 0.0F, 0.0F);
            static constexpr float kNoLights[16] = {};
            glUniform4fv(program.light_colour, 4, kNoLights);
            glUniform1i(program.texture, 0);
        }
        const auto [it, inserted] = dmc3_programs_.emplace(key, program);
        (void)inserted;
        return it->second.id != 0U ? &it->second : nullptr;
    }

    // Mipmaps and anisotropy from the Graphics settings, set when they change.
    void apply_sampler_options(const dmcresource::GpuViewOptions& options) {
        const int anisotropy = max_anisotropy_ >= 1.0F ? std::clamp(options.anisotropy, 1, 16) : 1;
        if (sampler_mipmaps_ == static_cast<int>(options.mipmaps) && sampler_anisotropy_ == anisotropy) return;
        glSamplerParameteri(nearest_, GL_TEXTURE_MIN_FILTER, options.mipmaps ? GL_NEAREST_MIPMAP_NEAREST : GL_NEAREST);
        glSamplerParameteri(smooth_, GL_TEXTURE_MIN_FILTER, options.mipmaps ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
        if (max_anisotropy_ >= 1.0F) {
            glSamplerParameterf(smooth_, GL_TEXTURE_MAX_ANISOTROPY_EXT,
                                std::min(static_cast<float>(anisotropy), max_anisotropy_));
        }
        sampler_mipmaps_ = static_cast<int>(options.mipmaps);
        sampler_anisotropy_ = anisotropy;
    }

    // Colour + depth/stencil target, multisampled as the settings ask when
    // the chip and the memory allow it (about 256 MiB of samples); a plain
    // target the samples resolve into and the pixels are read from.
    bool ensure_target(int width, int height, int requested_samples) {
        if (width > max_target_ || height > max_target_) return false;
        const auto pixels = static_cast<long long>(width) * height;
        const long long memory_cap = (256LL << 20) / (pixels * 8LL);
        int samples = 0;
        for (int s = 2; s <= 16; s *= 2) {
            if (s <= requested_samples && s <= max_samples_ && s <= memory_cap) samples = s;
        }
        // Compared with what was asked last time, so a target that had to
        // fall back to fewer samples is not rebuilt every frame.
        if (width == target_width_ && height == target_height_ && samples == target_request_ && fbo_ != 0U) {
            return true;
        }
        destroy_target();
        const int request = samples;
        for (;; samples = samples > 2 ? samples / 2 : 0) {
            if (build_target(width, height, samples)) {
                target_request_ = request;
                return true;
            }
            destroy_target();
            if (samples == 0) return false;
        }
    }

    bool build_target(int width, int height, int samples) {
        glGenFramebuffers(1, &fbo_);
        glGenRenderbuffers(1, &color_);
        glGenRenderbuffers(1, &depth_);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
        glBindRenderbuffer(GL_RENDERBUFFER, color_);
        glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_RGBA8, width, height);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, color_);
        glBindRenderbuffer(GL_RENDERBUFFER, depth_);
        glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_DEPTH24_STENCIL8, width, height);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, depth_);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE || glGetError() != GL_NO_ERROR) {
            return false;
        }
        if (samples > 0) {
            glGenFramebuffers(1, &resolve_fbo_);
            glGenRenderbuffers(1, &resolve_color_);
            glBindFramebuffer(GL_FRAMEBUFFER, resolve_fbo_);
            glBindRenderbuffer(GL_RENDERBUFFER, resolve_color_);
            glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, width, height);
            glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, resolve_color_);
            if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE ||
                glGetError() != GL_NO_ERROR) {
                return false;
            }
        }
        target_width_ = width;
        target_height_ = height;
        target_samples_ = samples;
        return true;
    }

    // ---- the photo camera's development ----------------------------------

    bool ensure_post_programs() {
        if (post_tried_) return post_ready_;
        post_tried_ = true;
        focus_program_ = link(kFullVertex, with_depth(kFocusFragment).c_str());
        dof_program_ = link(kFullVertex, with_depth(kDofFragment).c_str());
        look_program_ = link(kFullVertex, kLookFragment);
        sharpen_program_ = link(kFullVertex, kSharpenFragment);
        glGenVertexArrays(1, &full_vao_);
        curves_tex_ = make_texture(GL_RGBA8, 256, 1, GL_RGBA, GL_UNSIGNED_BYTE, GL_NEAREST);
        post_ready_ = focus_program_ != 0U && dof_program_ != 0U && look_program_ != 0U && sharpen_program_ != 0U &&
                      full_vao_ != 0U && curves_tex_ != 0U && glGetError() == GL_NO_ERROR;
        return post_ready_;
    }

    static GLuint make_texture(GLenum internal, int width, int height, GLenum format, GLenum type, GLint filter) {
        GLuint id = 0U;
        glGenTextures(1, &id);
        glBindTexture(GL_TEXTURE_2D, id);
        glTexImage2D(GL_TEXTURE_2D, 0, static_cast<GLint>(internal), width, height, 0, format, type, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindTexture(GL_TEXTURE_2D, 0U);
        return id;
    }

    static GLuint make_fbo(GLuint color, GLuint depth_stencil) {
        GLuint fbo = 0U;
        glGenFramebuffers(1, &fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color, 0);
        if (depth_stencil != 0U) {
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D, depth_stencil, 0);
        }
        const bool complete = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
        glBindFramebuffer(GL_FRAMEBUFFER, 0U);
        if (!complete) {
            glDeleteFramebuffers(1, &fbo);
            return 0U;
        }
        return fbo;
    }

    // The frame's colour and depth as textures, two more to pass between
    // passes, and the auto focus's 16 x 16.
    bool ensure_post_targets(int width, int height) {
        if (width == post_width_ && height == post_height_ && post_fbo_ != 0U) return true;
        destroy_post_targets();
        post_color_ = make_texture(GL_RGBA8, width, height, GL_RGBA, GL_UNSIGNED_BYTE, GL_LINEAR);
        post_depth_ = make_texture(GL_DEPTH24_STENCIL8, width, height, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8, GL_NEAREST);
        ping_ = make_texture(GL_RGBA8, width, height, GL_RGBA, GL_UNSIGNED_BYTE, GL_LINEAR);
        pong_ = make_texture(GL_RGBA8, width, height, GL_RGBA, GL_UNSIGNED_BYTE, GL_LINEAR);
        focus_tex_ = make_texture(GL_RGBA8, 16, 16, GL_RGBA, GL_UNSIGNED_BYTE, GL_NEAREST);
        post_fbo_ = make_fbo(post_color_, post_depth_);
        ping_fbo_ = make_fbo(ping_, 0U);
        pong_fbo_ = make_fbo(pong_, 0U);
        focus_fbo_ = make_fbo(focus_tex_, 0U);
        if (post_fbo_ == 0U || ping_fbo_ == 0U || pong_fbo_ == 0U || focus_fbo_ == 0U || glGetError() != GL_NO_ERROR) {
            destroy_post_targets();
            return false;
        }
        post_width_ = width;
        post_height_ = height;
        return true;
    }

    void destroy_post_targets() {
        const GLuint fbos[4] = {post_fbo_, ping_fbo_, pong_fbo_, focus_fbo_};
        const GLuint textures[5] = {post_color_, post_depth_, ping_, pong_, focus_tex_};
        glDeleteFramebuffers(4, fbos);
        glDeleteTextures(5, textures);
        post_fbo_ = ping_fbo_ = pong_fbo_ = focus_fbo_ = 0U;
        post_color_ = post_depth_ = ping_ = pong_ = focus_tex_ = 0U;
        post_width_ = post_height_ = 0;
        glGetError();
    }

    void full_pass(GLuint target, int width, int height) {
        glBindFramebuffer(GL_FRAMEBUFFER, target);
        glViewport(0, 0, width, height);
        glDrawArrays(GL_TRIANGLES, 0, 3);
    }

    static void bind_unit(GLuint unit, GLuint texture) {
        glActiveTexture(GL_TEXTURE0 + unit);
        glBindSampler(unit, 0U);
        glBindTexture(GL_TEXTURE_2D, texture);
    }

    // The development: the frame (resolved) and its depth, the auto focus
    // when it is asked for, the depth of field, the lens / polarizer / look,
    // sharpening, then the read back of the last.
    bool develop(const GpuViewFrame& frame, const dmcresource::CameraPost& post, RgbaImage& image,
                 GLuint* shown_fbo) {
        const int w = frame.width;
        const int h = frame.height;
        if (static_cast<long long>(w) * h > 4096LL * 4096LL) return false;
        if (!ensure_post_programs() || !ensure_post_targets(w, h)) return false;
        glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo_);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, post_fbo_);
        glBlitFramebuffer(0, 0, w, h, 0, 0, w, h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
        glBlitFramebuffer(0, 0, w, h, 0, 0, w, h, GL_DEPTH_BUFFER_BIT, GL_NEAREST);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_BLEND);
        glDepthMask(GL_FALSE);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glBindVertexArray(full_vao_);
        const float proj_z = frame.projection[2];
        const float proj_w = frame.projection[3];

        GLuint source = post_color_;
        if (post.depth_of_field) {
            float focus_mm = post.focus_mm;
            if (focus_mm == 0.0F) {
                // The auto focus: the median distance in the middle.
                glUseProgram(focus_program_);
                bind_unit(1U, post_depth_);
                glUniform1i(glGetUniformLocation(focus_program_, "u_depth"), 1);
                glUniform2f(glGetUniformLocation(focus_program_, "u_proj"), proj_z, proj_w);
                glUniform2i(glGetUniformLocation(focus_program_, "u_size"), w, h);
                glUniform1f(glGetUniformLocation(focus_program_, "u_half"), post.auto_focus_half);
                full_pass(focus_fbo_, 16, 16);
                std::array<std::uint8_t, 16U * 16U * 4U> packed{};
                glBindFramebuffer(GL_READ_FRAMEBUFFER, focus_fbo_);
                glPixelStorei(GL_PACK_ALIGNMENT, 1);
                glReadPixels(0, 0, 16, 16, GL_RGBA, GL_UNSIGNED_BYTE, packed.data());
                std::vector<float> near;
                for (std::size_t i = 0; i < packed.size(); i += 4U) {
                    const std::uint32_t v = packed[i] | (packed[i + 1U] << 8U) | (packed[i + 2U] << 16U) |
                                            (static_cast<std::uint32_t>(packed[i + 3U]) << 24U);
                    if (v != 0U) near.push_back(static_cast<float>(v) / 16.0F);
                }
                if (near.empty()) {
                    post.focus_found = 0.0F;  // the sky: infinity
                    focus_mm = -1.0F;
                } else {
                    std::nth_element(near.begin(), near.begin() + static_cast<std::ptrdiff_t>(near.size() / 2U),
                                     near.end());
                    post.focus_found = near[near.size() / 2U];
                    focus_mm = post.focus_found * post.unit_mm;
                }
            } else {
                post.focus_found = focus_mm > 0.0F ? focus_mm / post.unit_mm : 0.0F;
            }
            glUseProgram(dof_program_);
            bind_unit(0U, post_color_);
            bind_unit(1U, post_depth_);
            const auto at = [this](const char* name) { return glGetUniformLocation(dof_program_, name); };
            glUniform1i(at("u_color"), 0);
            glUniform1i(at("u_depth"), 1);
            glUniform2f(at("u_proj"), proj_z, proj_w);
            glUniform2i(at("u_size"), w, h);
            glUniform1f(at("u_focus_mm"), focus_mm);
            glUniform1f(at("u_f"), post.focal_mm);
            glUniform1f(at("u_k"), post.coc_k);
            glUniform1f(at("u_unit"), post.unit_mm);
            glUniform1f(at("u_px_per_mm"), post.px_per_mm);
            glUniform1f(at("u_max_r"), post.max_radius);
            glUniform1i(at("u_taps"), std::clamp(post.taps, 1, 128));
            glUniform1i(at("u_bright"), post.bright_bokeh ? 1 : 0);
            glUniform2fv(at("u_tap"), 128, post.tap_xy.data());
            full_pass(ping_fbo_, w, h);
            source = ping_;
        }

        // The lens, the polarizer and the look.
        std::array<std::uint8_t, 256U * 4U> curves{};
        for (std::size_t v = 0; v < 256U; ++v) {
            curves[v * 4U] = post.curves[v];
            curves[v * 4U + 1U] = post.curves[256U + v];
            curves[v * 4U + 2U] = post.curves[512U + v];
            curves[v * 4U + 3U] = 255U;
        }
        glBindTexture(GL_TEXTURE_2D, curves_tex_);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 256, 1, GL_RGBA, GL_UNSIGNED_BYTE, curves.data());
        glUseProgram(look_program_);
        bind_unit(0U, source);
        bind_unit(1U, curves_tex_);
        const auto at = [this](const char* name) { return glGetUniformLocation(look_program_, name); };
        glUniform1i(at("u_src"), 0);
        glUniform1i(at("u_curves"), 1);
        glUniform2i(at("u_size"), w, h);
        glUniform1f(at("u_k"), post.distortion_k);
        glUniform1f(at("u_fringe"), post.fringe);
        glUniform1i(at("u_pol"), post.polarizer ? 1 : 0);
        glUniform1f(at("u_pol_strength"), post.polarizer_strength);
        glUniform1f(at("u_ring"), post.polarizer_ring);
        glUniform1f(at("u_sat"), post.saturation);
        glUniform3f(at("u_tint"), post.tint[0], post.tint[1], post.tint[2]);
        glUniform1i(at("u_cinema"), post.cinema ? 1 : 0);
        glUniform1f(at("u_vignette"), post.vignette);
        glUniform1f(at("u_sigma"), post.noise_sigma);
        glUniform1i(at("u_grain"), std::max(1, post.grain));
        glUniform1i(at("u_chroma"), post.chroma_noise ? 1 : 0);
        glUniform1ui(at("u_seed"), post.seed);
        const GLuint look_target = source == ping_ ? pong_fbo_ : ping_fbo_;
        GLuint result = source == ping_ ? pong_ : ping_;
        GLuint result_fbo = look_target;
        full_pass(look_target, w, h);

        if (post.sharpen > 0.0F) {
            glUseProgram(sharpen_program_);
            bind_unit(0U, result);
            glUniform1i(glGetUniformLocation(sharpen_program_, "u_src"), 0);
            glUniform2i(glGetUniformLocation(sharpen_program_, "u_size"), w, h);
            glUniform1f(glGetUniformLocation(sharpen_program_, "u_amount"), post.sharpen);
            result_fbo = result == ping_ ? pong_fbo_ : ping_fbo_;
            result = result == ping_ ? pong_ : ping_;
            full_pass(result_fbo, w, h);
        }
        bind_unit(1U, 0U);
        bind_unit(0U, 0U);
        glBindVertexArray(0);
        glDepthMask(GL_TRUE);

        draw_overlay(frame, result_fbo);
        // Shown on the window: the result stays where it is.
        if (shown_fbo != nullptr) {
            *shown_fbo = result_fbo;
            return glGetError() == GL_NO_ERROR;
        }
        glBindFramebuffer(GL_READ_FRAMEBUFFER, result_fbo);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        image.width = w;
        image.height = h;
        image.pixels.resize(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4U);
        glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, image.pixels.data());
        return glGetError() == GL_NO_ERROR;
    }

    void destroy_target() {
        glBindFramebuffer(GL_FRAMEBUFFER, 0U);
        const GLuint fbos[2] = {fbo_, resolve_fbo_};
        const GLuint rbs[3] = {color_, depth_, resolve_color_};
        glDeleteFramebuffers(2, fbos);
        glDeleteRenderbuffers(3, rbs);
        fbo_ = resolve_fbo_ = color_ = depth_ = resolve_color_ = 0U;
        target_width_ = target_height_ = 0;
        target_samples_ = -1;
        target_request_ = -1;
        glGetError();
    }

    // The GL texture of an image, uploaded with mipmaps on first use.
    GLuint texture(const ImagePreview* image) {
        if (image == nullptr || !image->available()) return 0U;
        if (image->width > static_cast<std::uint32_t>(max_texture_) ||
            image->height > static_cast<std::uint32_t>(max_texture_)) {
            return 0U;
        }
        auto& entry = textures_[image];
        const auto signature = texel_signature(*image);
        if (entry.id == 0U || entry.data != image->rgba8.data() || entry.signature != signature) {
            if (entry.id == 0U) glGenTextures(1, &entry.id);
            glBindTexture(GL_TEXTURE_2D, entry.id);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, static_cast<GLsizei>(image->width),
                         static_cast<GLsizei>(image->height), 0, GL_RGBA, GL_UNSIGNED_BYTE, image->rgba8.data());
            glGenerateMipmap(GL_TEXTURE_2D);
            texture_bytes_ -= entry.bytes;
            entry.bytes = image->rgba8.size() * 4U / 3U;
            texture_bytes_ += entry.bytes;
            entry.data = image->rgba8.data();
            entry.signature = signature;
        }
        entry.last_frame = frame_number_;
        return entry.id;
    }

    // Textures unused for a while, or the oldest ones past the budget.
    void trim_textures() {
        constexpr std::size_t kBudget = 768U * 1024U * 1024U;
        for (auto it = textures_.begin(); it != textures_.end();) {
            const bool stale = frame_number_ - it->second.last_frame > 900U;
            const bool over = texture_bytes_ > kBudget && it->second.last_frame != frame_number_;
            if (stale || over) {
                glDeleteTextures(1, &it->second.id);
                texture_bytes_ -= it->second.bytes;
                it = textures_.erase(it);
            } else {
                ++it;
            }
        }
    }

    void bind_texture(GLint textured_uniform, const ImagePreview* image, bool smooth) {
        const GLuint id = texture(image);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, id);
        glBindSampler(0U, smooth ? smooth_ : nearest_);
        glUniform1i(textured_uniform, id != 0U ? 1 : 0);
    }

    void set_blend(GpuBatchKind kind, bool soft_alpha) {
        const bool additive = kind == GpuBatchKind::model_additive || kind == GpuBatchKind::room_additive;
        const bool subtractive = kind == GpuBatchKind::model_subtractive || kind == GpuBatchKind::room_subtractive;
        if (!additive && !subtractive && !soft_alpha) {
            glDisable(GL_BLEND);
            return;
        }
        glEnable(GL_BLEND);
        glBlendEquation(subtractive ? GL_FUNC_REVERSE_SUBTRACT : GL_FUNC_ADD);
        const bool room = kind == GpuBatchKind::room_additive || kind == GpuBatchKind::room_subtractive;
        if (additive || subtractive) {
            // Model: src * a; room: the shader already weighted it.
            glBlendFunc(room ? GL_ONE : GL_SRC_ALPHA, GL_ONE);
        } else {
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        }
    }

    void draw_batch(const GpuBatch& batch, int mode, bool smooth, const GpuViewFrame& frame) {
        bind_texture(u_.textured, batch.texture, smooth && !batch.nearest);
        glUniform1i(u_.mode, mode);
        glUniform1i(u_.colored, batch.colored ? 1 : 0);
        glUniform1i(u_.translucent, batch.translucent ? 1 : 0);
        glUniform2f(u_.light, batch.light_base, batch.light_gain);
        float su = 0.0F, sv = 0.0F;
        if (batch.scroll_slot != dmcresource::kNoTextureSlot) {
            for (const auto& s : frame.room_scrolls) {
                if (s.slot == batch.scroll_slot) {
                    su = s.u;
                    sv = s.v;
                    break;
                }
            }
        }
        glUniform2f(u_.scroll, su, sv);
        glDrawArrays(GL_TRIANGLES, static_cast<GLint>(batch.first), static_cast<GLsizei>(batch.count));
    }

    // A mesh batch in its pass: with the DMC3 pair Core chose for it
    // (dmc3_draw), else with the Reader's shader in `mode`. The Reader's
    // program and `reader_vao` are bound again when the batch leaves them.
    void draw_mesh_batch(const GpuBatch& batch, dmcresource::GpuPass pass, int mode, bool smooth,
                         const GpuViewFrame& frame, GLuint reader_vao, GLuint dmc3_vao,
                         const std::array<float, 16>& view) {
        const auto d = dmcresource::dmc3_draw(batch, pass, frame);
        const Dmc3Program* program = d.drawn() ? dmc3_program(d.vertex, d.fragment) : nullptr;
        if (program == nullptr) {
            if (in_dmc3_) {
                glUseProgram(mesh_program_);
                glBindVertexArray(reader_vao);
                in_dmc3_ = false;
            }
            ++reader_batches_;
            draw_batch(batch, mode, smooth, frame);
            return;
        }
        if (!in_dmc3_) {
            glBindVertexArray(dmc3_vao);
            dmc3_constants();
            in_dmc3_ = true;
        }
        glUseProgram(program->id);
        const auto vp = view_projection(view, frame.projection);
        glUniformMatrix4fv(program->view_projection, 1, GL_FALSE, vp.data());
        glUniformMatrix4fv(program->world_view_projection, 1, GL_FALSE, vp.data());
        glUniformMatrix4fv(program->light_directions, 1, GL_FALSE, d.light_directions.data());
        glUniformMatrix4fv(program->light_colours, 1, GL_FALSE, d.light_colours.data());
        glUniform4f(program->part_colour, d.part_colour, d.part_colour, d.part_colour, d.fog_amount);
        glUniform1f(program->part_alpha, d.part_alpha);
        glUniform3f(program->ambient, d.ambient, d.ambient, d.ambient);
        glUniform4fv(program->light_x, 1, d.light_x.data());
        glUniform4fv(program->light_y, 1, d.light_y.data());
        glUniform4fv(program->light_z, 1, d.light_z.data());
        glUniform4fv(program->light_falloff, 1, d.light_falloff.data());
        glUniform4fv(program->light_colour, 4, d.light_colour.data());
        glUniform3f(program->fog_colour, d.fog_colour[0], d.fog_colour[1], d.fog_colour[2]);
        glUniform2f(program->fog_coeffs, frame.dmc3.fog_coeffs[0], frame.dmc3.fog_coeffs[1]);
        glUniform1i(program->alpha_test, d.alpha_func == dmcresource::dmc3::AlphaFunc::off ? 0 : 1);
        glUniform1i(program->alpha_func, static_cast<GLint>(d.alpha_func));
        glUniform1f(program->alpha_ref, d.alpha_ref);
        float su = 0.0F, sv = 0.0F;
        if (batch.scroll_slot != dmcresource::kNoTextureSlot) {
            for (const auto& s : frame.room_scrolls) {
                if (s.slot == batch.scroll_slot) {
                    su = s.u;
                    sv = s.v;
                    break;
                }
            }
        }
        // u_uv_scroll is in the shaders' fixed point (1/4096).
        glUniform2f(program->uv_scroll, su * 4096.0F, sv * 4096.0F);
        bind_texture(-1, batch.texture, smooth && !batch.nearest);
        glDrawArrays(GL_TRIANGLES, static_cast<GLint>(batch.first), static_cast<GLsizei>(batch.count));
        ++dmc3_batches_;
        used_[static_cast<std::size_t>(d.vertex)] = true;
        used_[static_cast<std::size_t>(d.fragment)] = true;
    }

    // Back to the Reader's program after a run of DMC3 batches.
    void leave_dmc3() {
        if (!in_dmc3_) return;
        glUseProgram(mesh_program_);
        in_dmc3_ = false;
    }

    void publish_shader_stats() {
        dmcresource::GpuShaderStats stats;
        stats.compiled = dmc3_compiled_;
        stats.failed = dmc3_failed_;
        stats.first_failure = dmc3_first_failure_;
        stats.dmc3_batches = dmc3_batches_;
        stats.reader_batches = reader_batches_;
        for (std::size_t i = 0U; i < used_.size(); ++i) {
            if (used_[i]) stats.used.push_back(static_cast<dmcresource::dmc3::Shader>(i));
        }
        dmcresource::set_gpu_shader_stats(std::move(stats));
    }

    void upload_aux(const std::vector<dmcresource::Vec3>& points) {
        std::vector<GpuVertex> vertices(points.size());
        for (std::size_t i = 0U; i < points.size(); ++i) {
            vertices[i].x = points[i].x;
            vertices[i].y = points[i].y;
            vertices[i].z = points[i].z;
        }
        glBindBuffer(GL_ARRAY_BUFFER, aux_vbo_);
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(vertices.size() * sizeof(GpuVertex)), vertices.data(),
                     GL_STREAM_DRAW);
    }

    bool draw_current(const GpuViewFrame& frame, RgbaImage& image) {
        auto draw_timer = std::make_unique<frame_profile::Scope>(frame_profile::Stage::GpuDraw);
        ++frame_number_;
        glGetError();
        if (!ensure_target(frame.width, frame.height, frame.options.msaa_samples)) return false;
        apply_sampler_options(frame.options);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
        glViewport(0, 0, frame.width, frame.height);
        glDisable(GL_SCISSOR_TEST);
        glDisable(GL_CULL_FACE);
        glDisable(GL_STENCIL_TEST);
        glDisable(GL_POLYGON_OFFSET_FILL);
        glDepthMask(GL_TRUE);
        glStencilMask(0xFFU);
        glClearColor(frame.background[0] / 255.0F, frame.background[1] / 255.0F, frame.background[2] / 255.0F, 1.0F);
        glClearDepthf(1.0F);
        glClearStencil(0);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
        // The cleared alpha (1) stays: nothing draws into it, so the pixels
        // come back opaque without a pass over them.
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_FALSE);

        in_dmc3_ = false;
        dmc3_batches_ = 0U;
        reader_batches_ = 0U;
        used_.fill(false);
        glUseProgram(mesh_program_);
        glUniform4f(u_.proj, frame.projection[0], frame.projection[1], frame.projection[2], frame.projection[3]);
        glUniform1f(u_.cd, frame.camera_distance);
        glUniform1f(u_.radius, std::max(frame.radius, 1.0e-4F));
        const auto& fog = frame.fog;
        glUniform4f(u_.fog, fog.enabled ? 1.0F : 0.0F, fog.start, fog.end, 0.0F);
        glUniform4f(u_.fog_near, fog.near_colour[0], fog.near_colour[1], fog.near_colour[2], fog.near_colour[3]);
        glUniform4f(u_.fog_far, fog.far_colour[0], fog.far_colour[1], fog.far_colour[2], fog.far_colour[3]);
        glUniform1i(u_.tex, 0);
        glUniform1f(u_.depth_bias, 0.0F);
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LESS);

        // Room geometry: rebuilt only when the room changed.
        if (frame.room) {
            if (!room_cached_ || !(room_key_ == frame.room_key)) {
                const auto geometry = frame.build_room();
                glBindBuffer(GL_ARRAY_BUFFER, room_vbo_);
                glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(geometry.vertices.size() * sizeof(GpuVertex)),
                             geometry.vertices.data(), GL_STATIC_DRAW);
                room_batches_ = geometry.batches;
                room_key_ = frame.room_key;
                room_cached_ = true;
            }
        }

        // 1. Opaque room texels.
        if (frame.room) {
            glBindVertexArray(room_vao_);
            glUniformMatrix4fv(u_.view, 1, GL_FALSE, frame.room_view.data());
            glUniform1i(u_.room, 1);
            glDepthMask(GL_TRUE);
            glDisable(GL_BLEND);
            for (const auto& batch : room_batches_) {
                if (batch.kind != GpuBatchKind::room) continue;
                draw_mesh_batch(batch, dmcresource::GpuPass::room_opaque, 2, frame.smooth_room, frame, room_vao_,
                                room_dmc3_vao_, frame.room_view);
            }
            leave_dmc3();
        }

        // 2. Plain floor.
        glUniformMatrix4fv(u_.view, 1, GL_FALSE, frame.model_view.data());
        glUniform1i(u_.room, 0);
        glUniform2f(u_.light, 1.0F, 0.0F);
        glUniform2f(u_.scroll, 0.0F, 0.0F);
        if (!frame.floor.empty()) {
            upload_aux(frame.floor);
            glBindVertexArray(aux_vao_);
            glDisable(GL_BLEND);
            glUniform1i(u_.mode, 5);
            glUniform4f(u_.solid, frame.floor_rgb[0] / 255.0F, frame.floor_rgb[1] / 255.0F,
                        frame.floor_rgb[2] / 255.0F, 1.0F);
            glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(frame.floor.size()));
        }

        // 3. The model: blended-over batches writing depth, then additive /
        // subtractive ones on top without it.
        if (!frame.model.vertices.empty()) {
            glBindBuffer(GL_ARRAY_BUFFER, model_vbo_);
            glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(frame.model.vertices.size() * sizeof(GpuVertex)),
                         frame.model.vertices.data(), GL_STREAM_DRAW);
            glBindVertexArray(model_vao_);
            for (const auto& batch : frame.model.batches) {
                const bool layered = batch.kind != GpuBatchKind::model;
                glDepthMask(layered ? GL_FALSE : GL_TRUE);
                set_blend(batch.kind, !layered);
                draw_mesh_batch(batch, dmcresource::GpuPass::model, layered ? 1 : 0, frame.smooth_model, frame,
                                model_vao_, model_dmc3_vao_, frame.model_view);
            }
            leave_dmc3();
        }

        // 4. Soft room texels, then light shafts.
        if (frame.room) {
            glBindVertexArray(room_vao_);
            glUniformMatrix4fv(u_.view, 1, GL_FALSE, frame.room_view.data());
            glUniform1i(u_.room, 1);
            glDepthMask(GL_FALSE);
            for (const auto& batch : room_batches_) {
                if (batch.kind != GpuBatchKind::room || !batch.translucent) continue;
                set_blend(batch.kind, true);
                draw_mesh_batch(batch, dmcresource::GpuPass::room_soft, 3, frame.smooth_room, frame, room_vao_,
                                room_dmc3_vao_, frame.room_view);
            }
            for (const auto& batch : room_batches_) {
                if (batch.kind == GpuBatchKind::room) continue;
                set_blend(batch.kind, true);
                draw_mesh_batch(batch, dmcresource::GpuPass::room_layered, 4, frame.smooth_room, frame, room_vao_,
                                room_dmc3_vao_, frame.room_view);
            }
            leave_dmc3();
        }

        // 5. Shadow footprint: darkens what is visible there once per pixel.
        if (!frame.shadow.empty()) {
            upload_aux(frame.shadow);
            glBindVertexArray(aux_vao_);
            glUniformMatrix4fv(u_.view, 1, GL_FALSE, frame.model_view.data());
            glUniform1i(u_.room, 0);
            glUniform1i(u_.mode, 6);
            glUniform1f(u_.depth_bias, frame.radius * 0.01F);
            glDepthMask(GL_FALSE);
            glDepthFunc(GL_LEQUAL);
            glEnable(GL_STENCIL_TEST);
            glStencilFunc(GL_EQUAL, 0, 0xFFU);
            glStencilOp(GL_KEEP, GL_KEEP, GL_INCR);
            glEnable(GL_BLEND);
            glBlendEquation(GL_FUNC_ADD);
            glBlendFunc(GL_ZERO, GL_SRC_COLOR);
            glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(frame.shadow.size()));
            glDisable(GL_STENCIL_TEST);
            glUniform1f(u_.depth_bias, 0.0F);
            glDepthFunc(GL_LESS);
        }

        // 6. Effect quads (image space).
        if (!frame.effects.empty()) {
            std::vector<GpuScreenVertex> vertices;
            vertices.reserve(frame.effects.size() * 6U);
            for (const auto& quad : frame.effects) {
                for (const int k : {0, 1, 2, 0, 2, 3}) vertices.push_back(quad.vertices[static_cast<std::size_t>(k)]);
            }
            glUseProgram(screen_program_);
            glUniform2f(screen_size_, static_cast<float>(frame.width), static_cast<float>(frame.height));
            glUniform3f(screen_depth_, frame.projection[2], frame.projection[3], frame.camera_distance);
            glUniform1i(screen_tex_, 0);
            glBindVertexArray(screen_vao_);
            glBindBuffer(GL_ARRAY_BUFFER, screen_vbo_);
            glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(vertices.size() * sizeof(GpuScreenVertex)),
                         vertices.data(), GL_STREAM_DRAW);
            glDepthMask(GL_FALSE);
            glEnable(GL_BLEND);
            glBlendEquation(GL_FUNC_ADD);
            for (std::size_t i = 0U; i < frame.effects.size(); ++i) {
                const auto& quad = frame.effects[i];
                glBlendFunc(GL_SRC_ALPHA, quad.additive ? GL_ONE : GL_ONE_MINUS_SRC_ALPHA);
                bind_texture(screen_textured_, quad.texture, true);
                glDrawArrays(GL_TRIANGLES, static_cast<GLint>(i * 6U), 6);
            }
        }
        glBindVertexArray(0);
        glDisable(GL_BLEND);
        glDepthMask(GL_TRUE);

        // 7. A cutscene's fade over everything drawn.
        if (frame.fade[3] > 0.0F) {
            const float w = static_cast<float>(frame.width);
            const float h = static_cast<float>(frame.height);
            // Halfway into the depth range, so the quad is never clipped.
            const float z = 0.5F * (frame.near_z + frame.far_z) - frame.camera_distance;
            const std::array<float, 4> colour{frame.fade[0], frame.fade[1], frame.fade[2],
                                              std::clamp(frame.fade[3], 0.0F, 1.0F) * 255.0F};
            const std::array<GpuScreenVertex, 6> quad{{{0.0F, 0.0F, z, 0.0F, 0.0F, colour},
                                                       {w, 0.0F, z, 0.0F, 0.0F, colour},
                                                       {w, h, z, 0.0F, 0.0F, colour},
                                                       {0.0F, 0.0F, z, 0.0F, 0.0F, colour},
                                                       {w, h, z, 0.0F, 0.0F, colour},
                                                       {0.0F, h, z, 0.0F, 0.0F, colour}}};
            glUseProgram(screen_program_);
            glUniform2f(screen_size_, w, h);
            glUniform3f(screen_depth_, frame.projection[2], frame.projection[3], frame.camera_distance);
            glUniform1i(screen_textured_, 0);
            glBindVertexArray(screen_vao_);
            glBindBuffer(GL_ARRAY_BUFFER, screen_vbo_);
            glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(sizeof(quad)), quad.data(), GL_STREAM_DRAW);
            glDisable(GL_DEPTH_TEST);
            glEnable(GL_BLEND);
            glBlendEquation(GL_FUNC_ADD);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glDrawArrays(GL_TRIANGLES, 0, 6);
            glDisable(GL_BLEND);
            glEnable(GL_DEPTH_TEST);
            glBindVertexArray(0);
        }

        publish_shader_stats();
        // Submitting is done; the read back waits for the GPU to finish.
        draw_timer.reset();
        const frame_profile::Scope read_timer{frame_profile::Stage::Readback};
        const bool show = frame.present && window_ready();
        // The photo camera developed here, on the drawn frame and its depth.
        if (frame.post != nullptr && frame.post->enabled) {
            GLuint shown_fbo = 0U;
            const bool developed = develop(frame, *frame.post, image, show ? &shown_fbo : nullptr);
            frame.post->applied = developed;
            if (developed) {
                if (show) frame.presented = present(shown_fbo, frame.width, frame.height);
                glBindFramebuffer(GL_FRAMEBUFFER, 0U);
                glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
                trim_textures();
                // Not shown after all: read the developed frame back.
                if (show && !frame.presented) return read_back(shown_fbo, frame.width, frame.height, image);
                return glGetError() == GL_NO_ERROR;
            }
            glGetError();
        }
        // Resolve and read back: GL row 0 is image row 0 (the projection
        // flips y), so the rows land top-down as RgbaImage stores them.
        GLuint read_fbo = fbo_;
        if (target_samples_ > 0) {
            glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo_);
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, resolve_fbo_);
            glBlitFramebuffer(0, 0, frame.width, frame.height, 0, 0, frame.width, frame.height,
                              GL_COLOR_BUFFER_BIT, GL_NEAREST);
            read_fbo = resolve_fbo_;
        }
        draw_overlay(frame, read_fbo);
        // A development that did not run is the CPU's to do: it needs the pixels.
        const bool undeveloped = frame.post != nullptr && frame.post->enabled;
        if (show && !undeveloped && present(read_fbo, frame.width, frame.height)) {
            frame.presented = true;
            glBindFramebuffer(GL_FRAMEBUFFER, 0U);
            glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
            trim_textures();
            return glGetError() == GL_NO_ERROR;
        }
        glBindFramebuffer(GL_READ_FRAMEBUFFER, read_fbo);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        image.width = frame.width;
        image.height = frame.height;
        image.pixels.resize(static_cast<std::size_t>(frame.width) * static_cast<std::size_t>(frame.height) * 4U);
        glReadPixels(0, 0, frame.width, frame.height, GL_RGBA, GL_UNSIGNED_BYTE, image.pixels.data());
        glBindFramebuffer(GL_FRAMEBUFFER, 0U);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        trim_textures();
        return glGetError() == GL_NO_ERROR;
    }

    // The lines, crosses and labels over the finished frame in `fbo` (no
    // depth test; opaque, as the CPU draws them).
    void draw_overlay(const GpuViewFrame& frame, GLuint fbo) {
        const auto& overlay = frame.overlay;
        if (overlay.empty()) return;
        const float w = static_cast<float>(frame.width);
        const float h = static_cast<float>(frame.height);
        const float z = 0.5F * (frame.near_z + frame.far_z) - frame.camera_distance;
        std::vector<GpuScreenVertex> vertices = overlay.triangles;
        for (auto& v : vertices) v.z = z;
        const std::size_t solid = vertices.size();
        for (const auto& label : overlay.labels) {
            const float x0 = label.x, y0 = label.y;
            const float x1 = x0 + static_cast<float>(label.image.width);
            const float y1 = y0 + static_cast<float>(label.image.height);
            const std::array<float, 4> white{255.0F, 255.0F, 255.0F, 255.0F};
            const GpuScreenVertex corners[4] = {{x0, y0, z, 0.0F, 0.0F, white}, {x1, y0, z, 1.0F, 0.0F, white},
                                                {x1, y1, z, 1.0F, 1.0F, white}, {x0, y1, z, 0.0F, 1.0F, white}};
            for (const int k : {0, 1, 2, 0, 2, 3}) vertices.push_back(corners[k]);
        }
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glViewport(0, 0, frame.width, frame.height);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_STENCIL_TEST);
        glDisable(GL_CULL_FACE);
        glDepthMask(GL_FALSE);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_FALSE);
        glEnable(GL_BLEND);
        glBlendEquation(GL_FUNC_ADD);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glUseProgram(screen_program_);
        glUniform2f(screen_size_, w, h);
        glUniform3f(screen_depth_, frame.projection[2], frame.projection[3], frame.camera_distance);
        glUniform1i(screen_tex_, 0);
        glBindVertexArray(screen_vao_);
        glBindBuffer(GL_ARRAY_BUFFER, screen_vbo_);
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(vertices.size() * sizeof(GpuScreenVertex)),
                     vertices.data(), GL_STREAM_DRAW);
        if (solid > 0U) {
            glUniform1i(screen_textured_, 0);
            glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(solid));
        }
        // Labels: a few small textures, the same ones every frame.
        if (label_textures_.size() < overlay.labels.size()) {
            const std::size_t old = label_textures_.size();
            label_textures_.resize(overlay.labels.size());
            glGenTextures(static_cast<GLsizei>(label_textures_.size() - old), label_textures_.data() + old);
        }
        glActiveTexture(GL_TEXTURE0);
        glBindSampler(0U, nearest_);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        for (std::size_t i = 0U; i < overlay.labels.size(); ++i) {
            const auto& image = overlay.labels[i].image;
            if (!image.available()) continue;
            glBindTexture(GL_TEXTURE_2D, label_textures_[i]);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, static_cast<GLsizei>(image.width),
                         static_cast<GLsizei>(image.height), 0, GL_RGBA, GL_UNSIGNED_BYTE, image.rgba8.data());
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
            glUniform1i(screen_textured_, 1);
            glDrawArrays(GL_TRIANGLES, static_cast<GLint>(solid + i * 6U), 6);
        }
        glBindVertexArray(0);
        glDisable(GL_BLEND);
        glEnable(GL_DEPTH_TEST);
        glDepthMask(GL_TRUE);
    }

    bool read_back(GLuint fbo, int width, int height, RgbaImage& image) {
        glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        image.width = width;
        image.height = height;
        image.pixels.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4U);
        glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, image.pixels.data());
        glBindFramebuffer(GL_FRAMEBUFFER, 0U);
        return glGetError() == GL_NO_ERROR;
    }

    // ---- Showing frames on the view's window ----------------------------

    // The window's EGL surface, made on the first frame shown on it.
    bool window_ready() {
        if (window_ == nullptr || window_failed_ || !window_config_) return false;
        if (window_surface_ != EGL_NO_SURFACE) return true;
#ifdef __ANDROID__
        auto* native = static_cast<ANativeWindow*>(window_);
        EGLint format = 0;
        if (eglGetConfigAttrib(display_, config_, EGL_NATIVE_VISUAL_ID, &format) == EGL_TRUE) {
            ANativeWindow_setBuffersGeometry(native, 0, 0, format);
        }
        window_surface_ = eglCreateWindowSurface(display_, config_, native, nullptr);
#else
        // Off the phone (the desktop probe): a pbuffer of the given size
        // stands in for the window, and what is shown is read into it.
        const auto* stand_in = static_cast<const PresentStandIn*>(window_);
        const EGLint attributes[] = {EGL_WIDTH, stand_in->width, EGL_HEIGHT, stand_in->height, EGL_NONE};
        window_surface_ = eglCreatePbufferSurface(display_, config_, attributes);
#endif
        if (window_surface_ == EGL_NO_SURFACE) {
            window_failed_ = true;
            return false;
        }
        // Never wait for the display: the frame waiting on it holds the session.
        if (eglMakeCurrent(display_, window_surface_, window_surface_, context_) == EGL_TRUE) {
            eglSwapInterval(display_, 0);
        }
        make_current();
        return true;
    }

    // The frame in `fbo` (single-sampled, GL row 0 = image row 0) onto the
    // window, scaled to it and turned upright, then shown.
    bool present(GLuint fbo, int width, int height) {
        if (eglMakeCurrent(display_, window_surface_, window_surface_, context_) != EGL_TRUE) {
            make_current();
            window_failed_ = true;
            return false;
        }
        EGLint window_width = 0;
        EGLint window_height = 0;
        eglQuerySurface(display_, window_surface_, EGL_WIDTH, &window_width);
        eglQuerySurface(display_, window_surface_, EGL_HEIGHT, &window_height);
        bool ok = window_width > 0 && window_height > 0;
        if (ok) {
            glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo);
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0U);
            glDisable(GL_SCISSOR_TEST);
            glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
            const bool same = window_width == width && window_height == height;
            glBlitFramebuffer(0, 0, width, height, 0, window_height, window_width, 0, GL_COLOR_BUFFER_BIT,
                              same ? GL_NEAREST : GL_LINEAR);
            ok = glGetError() == GL_NO_ERROR;
#ifndef __ANDROID__
            if (ok) {
                auto* stand_in = static_cast<PresentStandIn*>(window_);
                stand_in->pixels.resize(static_cast<std::size_t>(window_width) * window_height * 4U);
                glBindFramebuffer(GL_READ_FRAMEBUFFER, 0U);
                glPixelStorei(GL_PACK_ALIGNMENT, 1);
                glReadPixels(0, 0, window_width, window_height, GL_RGBA, GL_UNSIGNED_BYTE, stand_in->pixels.data());
                ++stand_in->shown;
            }
#endif
            ok = ok && eglSwapBuffers(display_, window_surface_) == EGL_TRUE;
        }
        make_current();
        if (!ok) {
            // A window gone or refusing frames: back to reading frames back
            // until the view hands over a new one.
            drop_surface();
            window_failed_ = true;
        }
        return ok;
    }

    void drop_surface() {
        if (window_surface_ != EGL_NO_SURFACE) eglDestroySurface(display_, window_surface_);
        window_surface_ = EGL_NO_SURFACE;
    }

    void drop_window() {
        drop_surface();
#ifdef __ANDROID__
        if (window_ != nullptr) ANativeWindow_release(static_cast<ANativeWindow*>(window_));
#endif
        window_ = nullptr;
    }

    std::mutex mutex_;
    bool failed_{false};
    std::vector<GLuint> label_textures_;
    EGLConfig config_{};
    bool window_config_{false};
    void* window_{};
    bool window_failed_{false};
    EGLSurface window_surface_{EGL_NO_SURFACE};
    // The photo camera's passes and targets (made on first use).
    bool post_tried_{false};
    bool post_ready_{false};
    GLuint focus_program_{}, dof_program_{}, look_program_{}, sharpen_program_{};
    GLuint full_vao_{}, curves_tex_{};
    GLuint post_color_{}, post_depth_{}, ping_{}, pong_{}, focus_tex_{};
    GLuint post_fbo_{}, ping_fbo_{}, pong_fbo_{}, focus_fbo_{};
    int post_width_{}, post_height_{};
    unsigned failures_{0U};
    std::string description_;
    EGLDisplay display_{EGL_NO_DISPLAY};
    EGLContext context_{EGL_NO_CONTEXT};
    EGLSurface surface_{EGL_NO_SURFACE};

    GLuint mesh_program_{}, screen_program_{};
    Uniforms u_{};
    GLint screen_size_{}, screen_depth_{}, screen_tex_{}, screen_textured_{};
    GLuint model_vbo_{}, room_vbo_{}, aux_vbo_{}, screen_vbo_{};
    GLuint model_vao_{}, room_vao_{}, aux_vao_{}, screen_vao_{};
    // The DMC3 shaders: compiled objects by Shader, linked pairs, this frame.
    GLuint model_dmc3_vao_{}, room_dmc3_vao_{};
    std::array<GLuint, static_cast<std::size_t>(dmcresource::dmc3::Shader::count)> dmc3_objects_{};
    std::unordered_map<std::uint32_t, Dmc3Program> dmc3_programs_;
    int dmc3_compiled_{}, dmc3_failed_{};
    std::string dmc3_first_failure_;
    bool in_dmc3_{false};
    std::uint64_t dmc3_batches_{}, reader_batches_{};
    std::array<bool, static_cast<std::size_t>(dmcresource::dmc3::Shader::count)> used_{};
    GLuint nearest_{}, smooth_{};
    GLint max_samples_{0}, max_target_{4096}, max_texture_{4096};
    float max_anisotropy_{0.0F};
    int sampler_mipmaps_{-1}, sampler_anisotropy_{-1};

    GLuint fbo_{}, color_{}, depth_{}, resolve_fbo_{}, resolve_color_{};
    int target_width_{}, target_height_{}, target_samples_{-1}, target_request_{-1};

    bool room_cached_{false};
    dmcresource::GpuRoomKey room_key_{};
    std::vector<GpuBatch> room_batches_;

    std::unordered_map<const ImagePreview*, Texture> textures_;
    std::size_t texture_bytes_{};
    std::uint64_t frame_number_{};
};

}  // namespace

void install_gles_view_backend() {
    static GlesViewBackend backend;
    dmcresource::set_gpu_view_backend(&backend);
}

}  // namespace dmcviewer
