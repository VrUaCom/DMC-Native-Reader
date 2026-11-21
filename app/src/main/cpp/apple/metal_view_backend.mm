// The Metal view backend (macOS, iOS): draws render_view's GpuViewFrame on
// the graphics chip and reads the pixels back, the way the OpenGL ES 3
// backend does on Android (android/gles_view_backend.cpp). Same passes, same
// blending, same depth and stencil rules; the shaders are the Reader's own
// written in the Metal shading language and compiled from source when the
// backend starts (no offline `metal` compiler needed).
//
// Clip space: the core's projection is OpenGL's (z in -w..w, image row 0
// at y = -1). Metal wants z in 0..w and row 0 at y = +1, so every vertex
// stage maps z to (z + w) / 2 and negates y.
//
// The DMC3 shaders the core draws with (dmc3_draw) are here in MSL, with an
// enhanced per-pixel variant (GpuViewOptions::shading). A frame may go
// straight to the window's CAMetalLayer (set_window) instead of being read
// back. The photo camera's development (CameraPost) runs here too, as on
// Android.

#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <QuartzCore/QuartzCore.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <map>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "dmcresource/view_gpu.h"

namespace {

using dmcresource::GpuBatch;
using dmcresource::GpuBatchKind;
using dmcresource::GpuScreenVertex;
using dmcresource::GpuVertex;
using dmcresource::GpuViewFrame;
using dmcresource::ImagePreview;
using dmcresource::RgbaImage;

constexpr const char* kShaders = R"msl(
#include <metal_stdlib>
using namespace metal;

// ---- Models, rooms, floor and shadow (the Reader's mesh shader) -----------

struct MeshIn {
    float3 pos [[attribute(0)]];
    float2 uv [[attribute(1)]];
    float4 color [[attribute(2)]];
    float3 normal [[attribute(3)]];
    float light [[attribute(4)]];
};

struct MeshVertexUniforms {
    float4x4 view;
    float4 proj;
    float cd;
    int room;
    float2 light;
    float2 scroll;
    float depth_bias;
    float pad;
};

struct MeshOut {
    float4 pos [[position]];
    float2 uv;
    float4 color;
    float light;
    float z;
};

static float4 metal_clip(float4 p) {
    p.y = -p.y;
    p.z = 0.5 * (p.z + p.w);
    return p;
}

vertex MeshOut mesh_vertex(MeshIn in [[stage_in]], constant MeshVertexUniforms& u [[buffer(1)]]) {
    float3 c = (u.view * float4(in.pos, 1.0)).xyz;
    float term = in.light;
    if (u.room == 1) {
        float3 n = float3x3(u.view[0].xyz, u.view[1].xyz, u.view[2].xyz) * in.normal;
        float nl = length(n);
        float pl = length(c);
        term = (nl > 0.0 && pl > 0.0) ? abs(dot(n, c)) / (nl * pl) : 1.0;
    }
    MeshOut o;
    o.light = u.light.x + u.light.y * term;
    o.uv = in.uv + u.scroll;
    o.color = in.color * (255.0 / 128.0);
    o.z = c.z - u.cd;
    float4 p = float4(u.proj.x * c.x, u.proj.y * c.y, u.proj.z * c.z + u.proj.w, c.z);
    if (u.depth_bias != 0.0) {
        float zb = max(c.z - u.depth_bias, 1.0e-4);
        p.z = (u.proj.z + u.proj.w / zb) * c.z;
    }
    o.pos = metal_clip(p);
    return o;
}

struct MeshFragmentUniforms {
    int textured;
    int mode;      // 0 model, 1 model added, 2 room opaque, 3 room soft, 4 room added, 5 solid, 6 shadow
    int colored;
    int translucent;
    float radius;
    float cd;
    float2 pad;
    float4 solid;
    float4 fog;       // x on, y start, z end
    float4 fog_near;
    float4 fog_far;
};

static float4 fog_at(float vz, constant MeshFragmentUniforms& u) {
    if (u.fog.x < 0.5) return float4(0.0);
    float z = vz + u.cd;
    float span = u.fog.z - u.fog.y;
    float t = span > 1.0e-3 ? clamp((z - u.fog.y) / span, 0.0, 1.0) : (z >= u.fog.z ? 1.0 : 0.0);
    return float4(mix(u.fog_near.rgb, u.fog_far.rgb, t), clamp(mix(u.fog_near.a, u.fog_far.a, t), 0.0, 1.0));
}

fragment float4 mesh_fragment(MeshOut in [[stage_in]], constant MeshFragmentUniforms& u [[buffer(0)]],
                              texture2d<float> tex [[texture(0)]], sampler smp [[sampler(0)]]) {
    float4 fog = fog_at(in.z, u);
    if (u.mode == 5) return u.solid;
    if (u.mode == 6) return float4(0.45, 0.45, 0.45, 1.0);
    if (u.mode <= 1) {
        if (u.textured == 0) {
            float zn = 0.5 + 0.5 * tanh(-in.z / u.radius);
            float s = floor(145.0 + 80.0 * zn) / 255.0;
            return float4(s, s, min(1.0, s + 10.0 / 255.0), 1.0);
        }
        float4 t = tex.sample(smp, in.uv);
        if (u.colored == 1) t = min(t * in.color, float4(1.0));
        t.rgb = min(t.rgb * in.light, float3(1.0));
        if (t.a < 0.5 / 255.0) discard_fragment();
        t.rgb = u.mode == 1 ? t.rgb * (1.0 - fog.a) : mix(t.rgb, fog.rgb, fog.a);
        return t;
    }
    float4 t = u.textured == 1 ? tex.sample(smp, in.uv) : float4(128.0 / 255.0, 128.0 / 255.0, 128.0 / 255.0, 1.0);
    if (t.a < 7.5 / 255.0) discard_fragment();
    bool opaque = (u.mode == 2 || u.mode == 3) ? (t.a >= 239.5 / 255.0 || u.translucent == 0) : false;
    if (u.mode == 2) {
        if (!opaque) discard_fragment();
        if (u.translucent == 0 && t.a < 31.5 / 255.0) discard_fragment();
    } else if (opaque) {
        discard_fragment();
    }
    float3 c = t.rgb;
    if (u.colored == 1) c *= in.color.rgb;
    c *= in.light;
    if (u.mode == 4) {
        float k = t.a;
        if (u.colored == 1) k *= min(1.0, in.color.a);
        return float4(c * k * (1.0 - fog.a), 1.0);
    }
    return float4(mix(min(c, float3(1.0)), fog.rgb, fog.a), u.mode == 3 ? t.a : 1.0);
}

// ---- Effect quads, the fade and the overlays (image space) ----------------

struct ScreenIn {
    float3 pos [[attribute(0)]];
    float2 uv [[attribute(1)]];
    float4 color [[attribute(2)]];
};

struct ScreenUniforms {
    float2 size;
    float2 pad;
    float4 depth;  // x proj z, y proj w, z camera distance
    int textured;
    int pad2[3];
};

struct ScreenOut {
    float4 pos [[position]];
    float2 uv;
    float4 color;
};

vertex ScreenOut screen_vertex(ScreenIn in [[stage_in]], constant ScreenUniforms& u [[buffer(1)]]) {
    float cz = in.pos.z + u.depth.z;
    float4 p = float4(in.pos.x * 2.0 / u.size.x - 1.0, in.pos.y * 2.0 / u.size.y - 1.0,
                      u.depth.x + u.depth.y / cz, 1.0);
    ScreenOut o;
    o.pos = metal_clip(p);
    o.uv = in.uv;
    o.color = in.color / 255.0;
    return o;
}

fragment float4 screen_fragment(ScreenOut in [[stage_in]], constant ScreenUniforms& u [[buffer(0)]],
                                texture2d<float> tex [[texture(0)]], sampler smp [[sampler(0)]]) {
    float4 t = u.textured == 1 ? tex.sample(smp, in.uv) : float4(1.0);
    bool tinted = any(in.color < float4(254.5 / 255.0));
    if (tinted) t *= in.color;
    if (t.a < (tinted ? 1.5 : 7.5) / 255.0) discard_fragment();
    return t;
}

// ---- The DMC3 shaders the renderer draws with (shaders/dmc3, dmc3_draw) ----
// model_lit / effect_model_lit / stage_lit / stage_lit_fog with
// texture_modulate / texture_modulate_fog, written in MSL from the GLSL
// sources. The Reader's vertices come posed (every one follows bone 0, the
// identity, alone), so the skinning reduces to the vertex itself; the
// screen mapping is the identity and the UVs are in 0..1 (x 4096 undoes the
// shaders' fixed point). Vertex colours arrive as 0..1 for models and rooms
// (the stage shaders' 0..255 / 255).

struct Dmc3In {
    float3 position [[attribute(0)]];
    float3 normal [[attribute(1)]];
    float2 uv [[attribute(2)]];
    float4 colour [[attribute(3)]];
};

struct Dmc3Uniforms {
    float4x4 view_projection;
    float4x4 light_directions;
    float4x4 light_colours;
    float4 part_colour;      // rgb part colour, w the part's fog amount
    float4 part_alpha;       // x
    float4 light_x;
    float4 light_y;
    float4 light_z;
    float4 light_falloff;
    float4 light_colour[4];
    float4 ambient;          // xyz
    float4 fog_colour;       // xyz
    float4 fog_scroll;       // xy fog coefficients, zw uv scroll (0..1 units)
    int4 alpha;              // x test, y function
    float4 alpha_ref;        // x
    float4 camera;           // xyz the camera in the batch's coordinates (enhanced shading)
    int4 enhanced;           // x lighting: 0 model_lit, 1 effect_model_lit, 2 stage_lit; y specular on
};

struct Dmc3Out {
    float4 pos [[position]];
    float2 uv;
    float4 colour;
    float4 fog;
};

vertex Dmc3Out model_lit(Dmc3In in [[stage_in]], constant Dmc3Uniforms& u [[buffer(1)]]) {
    Dmc3Out o;
    o.pos = metal_clip(u.view_projection * float4(in.position, 1.0));
    float4 intensity = max(u.light_directions * float4(in.normal, 1.0), 0.0);
    float4 light = (u.light_colours * intensity) / 128.0;
    o.colour = clamp(float4(light.rgb * u.part_colour.rgb, u.part_alpha.x), 0.0, 1.0);
    o.uv = in.uv + u.fog_scroll.zw;
    o.fog = clamp(float4(u.fog_colour.rgb, u.part_colour.w), 0.0, 1.0);
    return o;
}

vertex Dmc3Out effect_model_lit(Dmc3In in [[stage_in]], constant Dmc3Uniforms& u [[buffer(1)]]) {
    Dmc3Out o;
    o.pos = metal_clip(u.view_projection * float4(in.position, 1.0));
    float4 intensity = clamp(u.light_directions * float4(in.normal, 1.0), 0.0, 1.0);
    float4 light = u.light_colours * intensity;
    float4 colour;
    colour.rgb = light.rgb * u.part_colour.rgb * in.colour.rgb * (255.0 / 128.0);
    colour.a = u.part_alpha.x;
    o.colour = clamp(colour, 0.0, 1.0);
    o.uv = in.uv + u.fog_scroll.zw;
    o.fog = clamp(float4(u.fog_colour.rgb, u.part_colour.w), 0.0, 1.0);
    return o;
}

static float4 stage_light(Dmc3In in, constant Dmc3Uniforms& u) {
    float4 dx = u.light_x - in.position.x;
    float4 dy = u.light_y - in.position.y;
    float4 dz = u.light_z - in.position.z;
    float4 dist2 = dx * dx + dy * dy + dz * dz;
    float4 atten = clamp(1.0 - dist2 * u.light_falloff, 0.0, 1.0);
    float4 facing = dx * in.normal.x + dy * in.normal.y + dz * in.normal.z;
    float4 w = max(facing * atten, 0.0);
    float4 light = u.light_colour[0] * w.x + u.light_colour[1] * w.y + u.light_colour[2] * w.z +
                   u.light_colour[3] * w.w;
    light.rgb += u.ambient.rgb;
    return clamp(float4(light.rgb * in.colour.rgb, u.part_alpha.x), 0.0, 1.0);
}

vertex Dmc3Out stage_lit(Dmc3In in [[stage_in]], constant Dmc3Uniforms& u [[buffer(1)]]) {
    Dmc3Out o;
    o.pos = metal_clip(u.view_projection * float4(in.position, 1.0));
    o.uv = in.uv + u.fog_scroll.zw;
    o.colour = stage_light(in, u);
    o.fog = float4(0.0, 0.0, 0.0, 1.0);
    return o;
}

vertex Dmc3Out stage_lit_fog(Dmc3In in [[stage_in]], constant Dmc3Uniforms& u [[buffer(1)]]) {
    Dmc3Out o;
    float4 c = u.view_projection * float4(in.position, 1.0);
    o.pos = metal_clip(c);
    o.uv = in.uv + u.fog_scroll.zw;
    o.colour = stage_light(in, u);
    o.fog = clamp(float4(u.fog_colour.rgb, u.fog_scroll.x + u.fog_scroll.y / abs(c.w)), 0.0, 1.0);
    return o;
}

static bool alpha_passes(float a, int func, float ref) {
    if (func == 1) return false;
    if (func == 2) return a < ref;
    if (func == 3) return a == ref;
    if (func == 4) return a <= ref;
    if (func == 5) return a > ref;
    if (func == 6) return a != ref;
    if (func == 7) return a >= ref;
    return true;
}

fragment float4 texture_modulate(Dmc3Out in [[stage_in]], constant Dmc3Uniforms& u [[buffer(0)]],
                                 texture2d<float> tex [[texture(0)]], sampler smp [[sampler(0)]]) {
    float4 c = tex.sample(smp, in.uv) * in.colour * 2.0;
    if (u.alpha.x != 0 && !alpha_passes(c.a, u.alpha.y, u.alpha_ref.x)) discard_fragment();
    return c;
}

fragment float4 texture_modulate_fog(Dmc3Out in [[stage_in]], constant Dmc3Uniforms& u [[buffer(0)]],
                                     texture2d<float> tex [[texture(0)]], sampler smp [[sampler(0)]]) {
    float4 c = tex.sample(smp, in.uv) * in.colour * 2.0;
    c.rgb = mix(in.fog.rgb, c.rgb, in.fog.a);
    if (u.alpha.x != 0 && !alpha_passes(c.a, u.alpha.y, u.alpha_ref.x)) discard_fragment();
    return c;
}
// ---- The finished frame onto the window's drawable (same size, BGRA).
struct PresentOut {
    float4 pos [[position]];
};

vertex PresentOut present_vertex(uint id [[vertex_id]]) {
    PresentOut o;
    float2 p = float2(float((id << 1) & 2), float(id & 2));
    o.pos = float4(p * 2.0 - 1.0, 0.0, 1.0);
    return o;
}

fragment float4 present_fragment(PresentOut in [[stage_in]], texture2d<float> frame [[texture(0)]]) {
    return float4(frame.read(uint2(in.pos.xy)).rgb, 1.0);
}

// ---- The photo camera's development (CameraPost), from the OpenGL ES
// backend's passes: the auto focus grid, the depth of field, the lens /
// polarizer / look, sharpening. Pixel (x, y) is image column x, row y, as
// there; the depth is the same 0..1 window depth.
static float post_distance(depth2d<float> depth, uint2 p, float2 proj) {
    float d = depth.read(p);
    if (d >= 0.999999) return -1.0;
    return proj.y / ((2.0 * d - 1.0) - proj.x);
}

struct FocusUniforms {
    int2 size;
    float2 proj;
    float half_side;
};

fragment float4 focus_fragment(PresentOut in [[stage_in]], constant FocusUniforms& u [[buffer(0)]],
                               depth2d<float> depth [[texture(0)]]) {
    float2 cell = floor(in.pos.xy);
    float2 p = float2(u.size) * 0.5 + (cell - 7.5) / 7.5 * u.half_side;
    float z = post_distance(depth, uint2(clamp(p, float2(0.0), float2(u.size) - 1.0)), u.proj);
    uint v = z < 0.0 ? 0u : uint(clamp(z * 16.0, 1.0, 4.0e9));
    return float4(float(v & 255u), float((v >> 8) & 255u), float((v >> 16) & 255u), float(v >> 24)) / 255.0;
}

struct DofUniforms {
    int2 size;
    float2 proj;
    float focus_mm;  // > 0 that distance, < 0 infinity
    float f;
    float k;
    float unit;
    float px_per_mm;
    float max_r;
    int taps;
    int bright;
    float2 tap[128];
};

static float dof_mm(float z, constant DofUniforms& u) { return z < 0.0 ? -1.0 : max(z * u.unit, u.f * 1.05); }

static float dof_radius(float zmm, constant DofUniforms& u) {
    float coc;
    if (u.focus_mm > 0.0) {
        float s = max(u.focus_mm, u.f * 1.05);
        coc = zmm < 0.0 ? u.k / (s - u.f) : u.k / (s - u.f) * abs(zmm - s) / zmm;
    } else {
        coc = zmm < 0.0 ? 0.0 : u.k / zmm;
    }
    return min(u.max_r, 0.5 * coc * u.px_per_mm);
}

fragment float4 dof_fragment(PresentOut in [[stage_in]], constant DofUniforms& u [[buffer(0)]],
                             texture2d<float> color [[texture(0)]], depth2d<float> depth [[texture(1)]]) {
    int2 p = int2(in.pos.xy);
    float4 c0 = color.read(uint2(p));
    float zp = dof_mm(post_distance(depth, uint2(p), u.proj), u);
    float r = dof_radius(zp, u);
    if (r < 0.5) return float4(c0.rgb, 1.0);
    int n = clamp(int(r * r * 1.5) + 4, 4, u.taps);
    float3 sum = float3(0.0);
    float weights = 0.0;
    for (int t = 0; t < 128; ++t) {
        if (t >= n) break;
        float2 d = u.tap[t] * r;
        int2 q = clamp(p + int2(round(d)), int2(0), u.size - 1);
        float zq = dof_mm(post_distance(depth, uint2(q), u.proj), u);
        float rq = dof_radius(zq, u);
        // A nearer pixel reaches over this one; a farther one no further than this one's own circle.
        bool nearer = zq >= 0.0 && (zp < 0.0 || zq < zp);
        float reach = nearer ? rq : min(rq, r);
        if (reach + 0.5 < length(d)) continue;
        float3 c = color.read(uint2(q)).rgb;
        float w = 1.0;
        if (u.bright == 1) w += 8.0 * max(0.0, dot(c, float3(0.2126, 0.7152, 0.0722)) - 0.75);
        sum += w * c;
        weights += w;
    }
    return float4(weights > 0.0 ? sum / weights : c0.rgb, 1.0);
}

struct LookUniforms {
    int2 size;
    float k;
    float fringe;
    int pol;
    float pol_strength;
    float ring;
    float sat;
    float4 tint;
    int cinema;
    float vignette;
    float sigma;
    int grain;
    int chroma;
    uint seed;
};

static float post_s2l(float v) { return v <= 0.04045 ? v / 12.92 : pow((v + 0.055) / 1.055, 2.4); }
static float post_l2s(float v) {
    v = clamp(v, 0.0, 1.0);
    return v <= 0.0031308 ? v * 12.92 : 1.055 * pow(v, 1.0 / 2.4) - 0.055;
}
static uint post_hash(uint x, uint y, uint seed) {
    uint h = x * 0x8DA6B343u ^ y * 0xD8163841u ^ seed * 0xCB1AB31Fu;
    h ^= h >> 13u;
    h *= 0x5BD1E995u;
    h ^= h >> 15u;
    return h;
}
static float post_noise(uint h) { return (float(h & 0xFFFFu) + float(h >> 16u)) / 65535.0 - 1.0; }

fragment float4 look_fragment(PresentOut in [[stage_in]], constant LookUniforms& u [[buffer(0)]],
                              constant uchar4* curves [[buffer(1)]], texture2d<float> src [[texture(0)]]) {
    constexpr sampler lin(coord::normalized, address::clamp_to_edge, filter::linear);
    float2 xy = floor(in.pos.xy);
    float2 size = float2(u.size);
    float2 c = size * 0.5;
    float hd = length(c);
    float2 d = (xy + 0.5 - c) / hd;
    float3 col;
    if (u.k == 0.0 && u.fringe == 0.0) {
        col = src.read(uint2(xy)).rgb;
    } else {
        // Distortion (the corners stay the corners) and fringing, radial.
        float scale = (1.0 + u.k * dot(d, d)) / (1.0 + u.k);
        col.g = src.sample(lin, (c + d * scale * hd) / size).g;
        col.r = src.sample(lin, (c + d * scale * (1.0 + u.fringe) * hd) / size).r;
        col.b = src.sample(lin, (c + d * scale * (1.0 - u.fringe) * hd) / size).b;
    }
    if (u.pol == 1) {
        float hi = max(col.r, max(col.g, col.b));
        float lo = min(col.r, min(col.g, col.b));
        float saturation = hi > 1.0e-3 ? (hi - lo) / hi : 0.0;
        float luma = dot(col, float3(0.2126, 0.7152, 0.0722));
        float glare = smoothstep(0.55, 0.95, luma) * (1.0 - saturation);
        float sky = clamp((col.b - max(col.r, col.g)) / max(col.b, 1.0e-3), 0.0, 1.0) * smoothstep(0.25, 0.7, col.b);
        float polarized = clamp(0.15 + 0.85 * glare + 0.6 * sky, 0.0, 0.95);
        float light = 0.5 * (xy.x / size.x - 0.5) + 0.3 * (xy.y / size.y - 0.5);
        float s = sin(u.ring - light);
        float held = u.pol_strength * polarized * s * s;
        float3 lin3 = float3(post_s2l(col.r), post_s2l(col.g), post_s2l(col.b)) * (1.0 - held);
        float l = dot(lin3, float3(0.2126, 0.7152, 0.0722));
        lin3 = l + (lin3 - l) * (1.0 + 0.8 * held);
        col = float3(post_l2s(lin3.r), post_l2s(lin3.g), post_l2s(lin3.b));
    }
    int3 i = int3(clamp(col * 255.0 + 0.5, 0.0, 255.0));
    float3 v = float3(float(curves[i.r].r), float(curves[i.g].g), float(curves[i.b].b));
    float luma = dot(v, float3(0.2126, 0.7152, 0.0722));
    if (u.sat != 0.0) v = luma + (v - luma) * (1.0 + u.sat);
    if (u.cinema == 1) {
        float t = luma / 255.0;
        v.r *= 0.92 + 0.16 * t;
        v.b *= 1.08 - 0.18 * t;
    } else {
        v *= u.tint.rgb;
    }
    if (u.vignette > 0.0) {
        float2 off = xy - c;
        float rr = dot(off, off) / dot(c, c);
        v *= 1.0 - u.vignette * rr * sqrt(rr);
    }
    if (u.sigma > 0.0) {
        float shade = 1.3 - 0.7 * clamp(luma / 255.0, 0.0, 1.0);
        uint gx = uint(xy.x) / uint(u.grain);
        uint gy = uint(xy.y) / uint(u.grain);
        v += post_noise(post_hash(gx, gy, u.seed)) * u.sigma * shade;
        if (u.chroma == 1) {
            v.r += post_noise(post_hash(gx, gy, u.seed ^ 0x9E3779B9u)) * u.sigma * 0.6 * shade;
            v.b += post_noise(post_hash(gx, gy, u.seed ^ 0x7F4A7C15u)) * u.sigma * 0.6 * shade;
        }
    }
    return float4(clamp((v + 0.5) / 255.0, 0.0, 1.0), 1.0);
}

struct SharpenUniforms {
    int2 size;
    float amount;
};

fragment float4 sharpen_fragment(PresentOut in [[stage_in]], constant SharpenUniforms& u [[buffer(0)]],
                                 texture2d<float> src [[texture(0)]]) {
    int2 p = int2(in.pos.xy);
    float3 c = src.read(uint2(p)).rgb;
    if (p.x == 0 || p.y == 0 || p.x == u.size.x - 1 || p.y == u.size.y - 1) return float4(c, 1.0);
    float3 around = src.read(uint2(p + int2(1, 0))).rgb + src.read(uint2(p - int2(1, 0))).rgb +
                    src.read(uint2(p + int2(0, 1))).rgb + src.read(uint2(p - int2(0, 1))).rgb;
    return float4(clamp(c + u.amount * (c - around * 0.25), 0.0, 1.0), 1.0);
}

// ---- Enhanced shading (the Graphics setting; off: the game's per-vertex
// light above). The same lights and the same formulas, evaluated for every
// pixel from the interpolated position and normal, so the light no longer
// follows the triangles; plus a soft highlight from the same lights (models:
// their directional rig; stages: the point lights, weaker). Effects get no
// highlight.

struct Dmc3PxOut {
    float4 pos [[position]];
    float2 uv;
    float4 colour;           // the vertex colour
    float4 fog;
    float3 p;                // the batch's coordinates
    float3 n;
};

static Dmc3PxOut px_common(Dmc3In in, constant Dmc3Uniforms& u, thread float4& clip) {
    Dmc3PxOut o;
    clip = u.view_projection * float4(in.position, 1.0);
    o.pos = metal_clip(clip);
    o.uv = in.uv + u.fog_scroll.zw;
    o.colour = in.colour;
    o.p = in.position;
    o.n = in.normal;
    return o;
}

vertex Dmc3PxOut model_lit_px(Dmc3In in [[stage_in]], constant Dmc3Uniforms& u [[buffer(1)]]) {
    float4 c;
    Dmc3PxOut o = px_common(in, u, c);
    o.fog = clamp(float4(u.fog_colour.rgb, u.part_colour.w), 0.0, 1.0);
    return o;
}

vertex Dmc3PxOut stage_lit_px(Dmc3In in [[stage_in]], constant Dmc3Uniforms& u [[buffer(1)]]) {
    float4 c;
    Dmc3PxOut o = px_common(in, u, c);
    o.fog = float4(0.0, 0.0, 0.0, 1.0);
    return o;
}

vertex Dmc3PxOut stage_lit_fog_px(Dmc3In in [[stage_in]], constant Dmc3Uniforms& u [[buffer(1)]]) {
    float4 c;
    Dmc3PxOut o = px_common(in, u, c);
    o.fog = clamp(float4(u.fog_colour.rgb, u.fog_scroll.x + u.fog_scroll.y / abs(c.w)), 0.0, 1.0);
    return o;
}

// The vertex shader's colour for this pixel, and the highlight to add.
static float4 px_light(Dmc3PxOut in, constant Dmc3Uniforms& u, thread float3& spec) {
    spec = float3(0.0);
    float len = length(in.n);
    float3 n = len > 1.0e-6 ? in.n / len : float3(0.0, 0.0, 1.0);
    float3 v = normalize(u.camera.xyz - in.p);
    if (u.enhanced.x == 2) {
        float4 dx = u.light_x - in.p.x;
        float4 dy = u.light_y - in.p.y;
        float4 dz = u.light_z - in.p.z;
        float4 dist2 = dx * dx + dy * dy + dz * dz;
        float4 atten = clamp(1.0 - dist2 * u.light_falloff, 0.0, 1.0);
        float4 facing = dx * n.x + dy * n.y + dz * n.z;
        float4 w = max(facing * atten, 0.0);
        float4 light = u.light_colour[0] * w.x + u.light_colour[1] * w.y + u.light_colour[2] * w.z +
                       u.light_colour[3] * w.w;
        light.rgb += u.ambient.rgb;
        if (u.enhanced.y != 0) {
            for (int i = 0; i < 4; ++i) {
                float3 l = float3(dx[i], dy[i], dz[i]);
                float d = length(l);
                if (d < 1.0e-4 || atten[i] <= 0.0) continue;
                l /= d;
                if (dot(n, l) <= 0.0) continue;
                float h = max(dot(n, normalize(l + v)), 0.0);
                spec += u.light_colour[i].rgb * min(d, 64.0) * atten[i] * pow(h, 24.0) * 0.12;
            }
        }
        return clamp(float4(light.rgb * in.colour.rgb, u.part_alpha.x), 0.0, 1.0);
    }
    float4 nn = float4(n, 1.0);
    float4 lit = u.light_directions * nn;
    float4 light;
    float4 colour;
    float unit;
    if (u.enhanced.x == 1) {
        unit = 1.0;
        light = u.light_colours * clamp(lit, 0.0, 1.0);
        colour.rgb = light.rgb * u.part_colour.rgb * in.colour.rgb * (255.0 / 128.0);
    } else {
        unit = 1.0 / 128.0;
        light = (u.light_colours * max(lit, 0.0)) * unit;
        colour.rgb = light.rgb * u.part_colour.rgb;
    }
    colour.a = u.part_alpha.x;
    if (u.enhanced.y != 0) {
        for (int i = 0; i < 3; ++i) {
            float3 l = float3(u.light_directions[0][i], u.light_directions[1][i], u.light_directions[2][i]);
            float d = length(l);
            if (d < 1.0e-4 || lit[i] <= 0.0) continue;
            l /= d;
            float h = max(dot(n, normalize(l + v)), 0.0);
            spec += u.light_colours[i].rgb * unit * d * pow(h, 32.0) * 0.4;
        }
        // A faint rim where the surface turns away from the camera.
        float rim = pow(1.0 - clamp(dot(n, v), 0.0, 1.0), 3.0);
        spec += colour.rgb * rim * 0.25;
    }
    return clamp(colour, 0.0, 1.0);
}

static float4 px_shade(Dmc3PxOut in, constant Dmc3Uniforms& u, texture2d<float> tex, sampler smp, bool fog) {
    float3 spec;
    float4 lit = px_light(in, u, spec);
    float4 t = tex.sample(smp, in.uv);
    float4 c = t * lit * 2.0;
    c.rgb += spec * t.a;
    if (fog) c.rgb = mix(in.fog.rgb, c.rgb, in.fog.a);
    c = clamp(c, 0.0, 1.0);
    if (u.alpha.x != 0 && !alpha_passes(c.a, u.alpha.y, u.alpha_ref.x)) discard_fragment();
    return c;
}

fragment float4 texture_modulate_px(Dmc3PxOut in [[stage_in]], constant Dmc3Uniforms& u [[buffer(0)]],
                                    texture2d<float> tex [[texture(0)]], sampler smp [[sampler(0)]]) {
    return px_shade(in, u, tex, smp, false);
}

fragment float4 texture_modulate_fog_px(Dmc3PxOut in [[stage_in]], constant Dmc3Uniforms& u [[buffer(0)]],
                                        texture2d<float> tex [[texture(0)]], sampler smp [[sampler(0)]]) {
    return px_shade(in, u, tex, smp, true);
}
)msl";

struct Dmc3Uniforms {
    std::array<float, 16> view_projection;
    std::array<float, 16> light_directions;
    std::array<float, 16> light_colours;
    std::array<float, 4> part_colour;
    std::array<float, 4> part_alpha;
    std::array<float, 4> light_x;
    std::array<float, 4> light_y;
    std::array<float, 4> light_z;
    std::array<float, 4> light_falloff;
    std::array<float, 16> light_colour;
    std::array<float, 4> ambient;
    std::array<float, 4> fog_colour;
    std::array<float, 4> fog_scroll;
    std::array<std::int32_t, 4> alpha;
    std::array<float, 4> alpha_ref;
    std::array<float, 4> camera;
    std::array<std::int32_t, 4> enhanced;
};

struct FocusUniforms {
    std::array<std::int32_t, 2> size;
    std::array<float, 2> proj;
    float half_side;
};

struct DofUniforms {
    std::array<std::int32_t, 2> size;
    std::array<float, 2> proj;
    float focus_mm, f, k, unit, px_per_mm, max_r;
    std::int32_t taps, bright;
    std::array<float, 256> tap;
};
static_assert(sizeof(DofUniforms) == 48 + 1024);

struct LookUniforms {
    std::array<std::int32_t, 2> size;
    float k, fringe;
    std::int32_t pol;
    float pol_strength, ring, sat;
    std::array<float, 4> tint;
    std::int32_t cinema;
    float vignette, sigma;
    std::int32_t grain, chroma;
    std::uint32_t seed;
    std::array<std::uint32_t, 2> pad;
};
static_assert(sizeof(LookUniforms) == 80);

struct SharpenUniforms {
    std::array<std::int32_t, 2> size;
    float amount;
};

// The DMC3 shader's function in the library (null: not written for Metal yet;
// the Reader's own shader draws that batch).
const char* dmc3_function(dmcresource::dmc3::Shader shader) {
    using dmcresource::dmc3::Shader;
    switch (shader) {
        case Shader::model_lit_vert: return "model_lit";
        case Shader::effect_model_lit_vert: return "effect_model_lit";
        case Shader::stage_lit_vert: return "stage_lit";
        case Shader::stage_lit_fog_vert: return "stage_lit_fog";
        case Shader::texture_modulate_frag: return "texture_modulate";
        case Shader::texture_modulate_fog_frag: return "texture_modulate_fog";
        default: return nullptr;
    }
}

// The enhanced-shading function in place of a DMC3 one (per-pixel light).
const char* dmc3_enhanced_function(dmcresource::dmc3::Shader shader) {
    using dmcresource::dmc3::Shader;
    switch (shader) {
        case Shader::model_lit_vert:
        case Shader::effect_model_lit_vert: return "model_lit_px";
        case Shader::stage_lit_vert: return "stage_lit_px";
        case Shader::stage_lit_fog_vert: return "stage_lit_fog_px";
        case Shader::texture_modulate_frag: return "texture_modulate_px";
        case Shader::texture_modulate_fog_frag: return "texture_modulate_fog_px";
        default: return nullptr;
    }
}

// The camera in the coordinates `view` (column-major, rigid) maps to camera space.
std::array<float, 4> camera_position(const std::array<float, 16>& v) noexcept {
    std::array<float, 4> c{};
    for (std::size_t i = 0U; i < 3U; ++i) {
        c[i] = -(v[i * 4U + 0U] * v[12] + v[i * 4U + 1U] * v[13] + v[i * 4U + 2U] * v[14]);
    }
    return c;
}

// The DMC3 shaders' u_view_projection: the core's projection after the view.
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

struct MeshVertexUniforms {
    std::array<float, 16> view;
    std::array<float, 4> proj;
    float cd;
    std::int32_t room;
    std::array<float, 2> light;
    std::array<float, 2> scroll;
    float depth_bias;
    float pad;
};

struct MeshFragmentUniforms {
    std::int32_t textured;
    std::int32_t mode;
    std::int32_t colored;
    std::int32_t translucent;
    float radius;
    float cd;
    std::array<float, 2> pad;
    std::array<float, 4> solid;
    std::array<float, 4> fog;
    std::array<float, 4> fog_near;
    std::array<float, 4> fog_far;
};

struct ScreenUniforms {
    std::array<float, 2> size;
    std::array<float, 2> pad;
    std::array<float, 4> depth;
    std::int32_t textured;
    std::array<std::int32_t, 3> pad2;
};

enum class Blend : std::uint8_t { none, alpha, add_src_alpha, add_one, sub_src_alpha, sub_one, multiply };

std::uint64_t texel_signature(const ImagePreview& image) noexcept {
    std::uint64_t h = 0xCBF29CE484222325ULL;
    const auto n = image.rgba8.size();
    for (std::size_t k = 0U; k < 32U && n != 0U; ++k) {
        h ^= image.rgba8[k * (n - 1U) / 31U];
        h *= 0x100000001B3ULL;
    }
    return h ^ (static_cast<std::uint64_t>(image.width) << 32U) ^ image.height;
}

class MetalViewBackend final : public dmcresource::GpuViewBackend {
public:
    bool draw(const GpuViewFrame& frame, RgbaImage& image) override {
        const std::lock_guard lock{mutex_};
        if (!initialize() || frame.width <= 0 || frame.height <= 0) return false;
        @autoreleasepool {
            const bool ok = draw_frame(frame, image);
            if (!ok && ++failures_ >= 8U) failed_ = true;
            if (ok) failures_ = 0U;
            return ok;
        }
    }

    // The view's CAMetalLayer (null: none): frames marked `present` go
    // onto its drawables. The backend holds the layer until replaced.
    void set_window(void* window) override {
        const std::lock_guard lock{mutex_};
        CAMetalLayer* layer = (__bridge CAMetalLayer*)window;
        if (layer != nil && initialize()) {
            layer.device = device_;
            layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
            layer.framebufferOnly = YES;
            layer_ = layer;
        } else {
            layer_ = nil;
        }
    }

    std::string describe() override {
        const std::lock_guard lock{mutex_};
        if (!initialize()) return {};
        return "Metal / " + std::string{[[device_ name] UTF8String]};
    }

    dmcresource::GpuCapabilities capabilities() override {
        const std::lock_guard lock{mutex_};
        if (!initialize()) return {};
        int samples = 1;
        for (const int n : {8, 4, 2}) {
            if ([device_ supportsTextureSampleCount:static_cast<NSUInteger>(n)]) {
                samples = n;
                break;
            }
        }
        return {samples, 16.0F};
    }

private:
    struct Texture final {
        id<MTLTexture> texture{};
        const void* data{};
        std::uint64_t signature{};
        std::uint64_t last_frame{};
        std::size_t bytes{};
    };

    bool initialize() {
        if (failed_) return false;
        if (device_ != nil) return true;
        device_ = MTLCreateSystemDefaultDevice();
        if (device_ == nil) {
            failed_ = true;
            return false;
        }
        queue_ = [device_ newCommandQueue];
        NSError* error = nil;
        library_ = [device_ newLibraryWithSource:[NSString stringWithUTF8String:kShaders] options:nil error:&error];
        if (library_ == nil) {
            NSLog(@"Metal view backend: shaders did not compile: %@", error);
            failed_ = true;
            return false;
        }
        mesh_vertex_ = [library_ newFunctionWithName:@"mesh_vertex"];
        mesh_fragment_ = [library_ newFunctionWithName:@"mesh_fragment"];
        screen_vertex_ = [library_ newFunctionWithName:@"screen_vertex"];
        screen_fragment_ = [library_ newFunctionWithName:@"screen_fragment"];

        mesh_layout_ = [MTLVertexDescriptor vertexDescriptor];
        const auto attribute = [](MTLVertexDescriptor* d, int index, MTLVertexFormat format, std::size_t offset) {
            d.attributes[index].format = format;
            d.attributes[index].offset = offset;
            d.attributes[index].bufferIndex = 0;
        };
        attribute(mesh_layout_, 0, MTLVertexFormatFloat3, offsetof(GpuVertex, x));
        attribute(mesh_layout_, 1, MTLVertexFormatFloat2, offsetof(GpuVertex, u));
        attribute(mesh_layout_, 2, MTLVertexFormatUChar4Normalized, offsetof(GpuVertex, rgba));
        attribute(mesh_layout_, 3, MTLVertexFormatFloat3, offsetof(GpuVertex, nx));
        attribute(mesh_layout_, 4, MTLVertexFormatFloat, offsetof(GpuVertex, light));
        mesh_layout_.layouts[0].stride = sizeof(GpuVertex);

        dmc3_layout_ = [MTLVertexDescriptor vertexDescriptor];
        attribute(dmc3_layout_, 0, MTLVertexFormatFloat3, offsetof(GpuVertex, x));
        attribute(dmc3_layout_, 1, MTLVertexFormatFloat3, offsetof(GpuVertex, nx));
        attribute(dmc3_layout_, 2, MTLVertexFormatFloat2, offsetof(GpuVertex, u));
        attribute(dmc3_layout_, 3, MTLVertexFormatUChar4Normalized, offsetof(GpuVertex, rgba));
        dmc3_layout_.layouts[0].stride = sizeof(GpuVertex);

        screen_layout_ = [MTLVertexDescriptor vertexDescriptor];
        attribute(screen_layout_, 0, MTLVertexFormatFloat3, offsetof(GpuScreenVertex, x));
        attribute(screen_layout_, 1, MTLVertexFormatFloat2, offsetof(GpuScreenVertex, u));
        attribute(screen_layout_, 2, MTLVertexFormatFloat4, offsetof(GpuScreenVertex, rgba));
        screen_layout_.layouts[0].stride = sizeof(GpuScreenVertex);

        // A 1x1 white texture for draws without one (the shaders ignore it).
        MTLTextureDescriptor* d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                                                                     width:1
                                                                                    height:1
                                                                                 mipmapped:NO];
        blank_ = [device_ newTextureWithDescriptor:d];
        const std::uint32_t white = 0xFFFFFFFFU;
        [blank_ replaceRegion:MTLRegionMake2D(0, 0, 1, 1) mipmapLevel:0 withBytes:&white bytesPerRow:4];
        return true;
    }

    id<MTLRenderPipelineState> pipeline(bool screen, Blend blend, NSUInteger samples) {
        return pipeline(screen ? "screen_vertex" : "mesh_vertex", screen ? "screen_fragment" : "mesh_fragment",
                        screen ? screen_layout_ : mesh_layout_, blend, samples);
    }

    id<MTLRenderPipelineState> pipeline(const char* vertex, const char* fragment, MTLVertexDescriptor* layout,
                                        Blend blend, NSUInteger samples) {
        const std::string key = std::string{vertex} + "|" + fragment + "|" + std::to_string(static_cast<int>(blend)) +
                                "|" + std::to_string(samples);
        auto& slot = pipelines_[key];
        if (slot != nil) return slot;
        MTLRenderPipelineDescriptor* p = [[MTLRenderPipelineDescriptor alloc] init];
        p.vertexFunction = function(vertex);
        p.fragmentFunction = function(fragment);
        if (p.vertexFunction == nil || p.fragmentFunction == nil) return nil;
        p.vertexDescriptor = layout;
        p.rasterSampleCount = samples;
        p.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float_Stencil8;
        p.stencilAttachmentPixelFormat = MTLPixelFormatDepth32Float_Stencil8;
        auto* c = p.colorAttachments[0];
        c.pixelFormat = MTLPixelFormatRGBA8Unorm;
        // The cleared alpha (1) stays: the pixels come back opaque.
        c.writeMask = MTLColorWriteMaskRed | MTLColorWriteMaskGreen | MTLColorWriteMaskBlue;
        if (blend != Blend::none) {
            c.blendingEnabled = YES;
            const bool sub = blend == Blend::sub_src_alpha || blend == Blend::sub_one;
            c.rgbBlendOperation = sub ? MTLBlendOperationReverseSubtract : MTLBlendOperationAdd;
            c.alphaBlendOperation = c.rgbBlendOperation;
            switch (blend) {
                case Blend::alpha:
                    c.sourceRGBBlendFactor = MTLBlendFactorSourceAlpha;
                    c.destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
                    break;
                case Blend::add_src_alpha:
                case Blend::sub_src_alpha:
                    c.sourceRGBBlendFactor = MTLBlendFactorSourceAlpha;
                    c.destinationRGBBlendFactor = MTLBlendFactorOne;
                    break;
                case Blend::add_one:
                case Blend::sub_one:
                    c.sourceRGBBlendFactor = MTLBlendFactorOne;
                    c.destinationRGBBlendFactor = MTLBlendFactorOne;
                    break;
                case Blend::multiply:
                    c.sourceRGBBlendFactor = MTLBlendFactorZero;
                    c.destinationRGBBlendFactor = MTLBlendFactorSourceColor;
                    break;
                case Blend::none:
                    break;
            }
            c.sourceAlphaBlendFactor = c.sourceRGBBlendFactor;
            c.destinationAlphaBlendFactor = c.destinationRGBBlendFactor;
        }
        NSError* error = nil;
        slot = [device_ newRenderPipelineStateWithDescriptor:p error:&error];
        if (slot == nil) NSLog(@"Metal view backend: pipeline: %@", error);
        return slot;
    }

    id<MTLFunction> function(const char* name) {
        auto& slot = functions_[name];
        if (slot == nil) slot = [library_ newFunctionWithName:[NSString stringWithUTF8String:name]];
        return slot;
    }

    // depth: 0 off, 1 less, 2 less-equal; stencil: the shadow's once-per-pixel count.
    id<MTLDepthStencilState> depth_state(int depth, bool write, bool stencil) {
        const int key = depth | (write ? 4 : 0) | (stencil ? 8 : 0);
        auto& slot = depth_states_[key];
        if (slot != nil) return slot;
        MTLDepthStencilDescriptor* d = [[MTLDepthStencilDescriptor alloc] init];
        d.depthCompareFunction = depth == 0   ? MTLCompareFunctionAlways
                                 : depth == 1 ? MTLCompareFunctionLess
                                              : MTLCompareFunctionLessEqual;
        d.depthWriteEnabled = write;
        if (stencil) {
            MTLStencilDescriptor* s = [[MTLStencilDescriptor alloc] init];
            s.stencilCompareFunction = MTLCompareFunctionEqual;
            s.stencilFailureOperation = MTLStencilOperationKeep;
            s.depthFailureOperation = MTLStencilOperationKeep;
            s.depthStencilPassOperation = MTLStencilOperationIncrementClamp;
            s.readMask = 0xFF;
            s.writeMask = 0xFF;
            d.frontFaceStencil = s;
            d.backFaceStencil = s;
        }
        slot = [device_ newDepthStencilStateWithDescriptor:d];
        return slot;
    }

    id<MTLSamplerState> sampler(bool smooth, const dmcresource::GpuViewOptions& options) {
        const int key = (smooth ? 1 : 0) | (options.mipmaps ? 2 : 0) | (std::clamp(options.anisotropy, 1, 16) << 2);
        auto& slot = samplers_[key];
        if (slot != nil) return slot;
        MTLSamplerDescriptor* d = [[MTLSamplerDescriptor alloc] init];
        d.sAddressMode = MTLSamplerAddressModeRepeat;
        d.tAddressMode = MTLSamplerAddressModeRepeat;
        d.minFilter = smooth ? MTLSamplerMinMagFilterLinear : MTLSamplerMinMagFilterNearest;
        d.magFilter = d.minFilter;
        d.mipFilter = !options.mipmaps ? MTLSamplerMipFilterNotMipmapped
                      : smooth         ? MTLSamplerMipFilterLinear
                                       : MTLSamplerMipFilterNearest;
        d.maxAnisotropy = smooth ? static_cast<NSUInteger>(std::clamp(options.anisotropy, 1, 16)) : 1;
        slot = [device_ newSamplerStateWithDescriptor:d];
        return slot;
    }

    bool ensure_target(int width, int height, int msaa) {
        NSUInteger samples = 1;
        for (const int n : {std::min(msaa, 8), 4, 2}) {
            if (n > 1 && [device_ supportsTextureSampleCount:static_cast<NSUInteger>(n)]) {
                samples = static_cast<NSUInteger>(n);
                break;
            }
        }
        if (msaa <= 1) samples = 1;
        if (color_ != nil && width_ == width && height_ == height && samples_ == samples) return true;
        width_ = width;
        height_ = height;
        samples_ = samples;
        const auto make = [this](MTLPixelFormat format, NSUInteger count) {
            MTLTextureDescriptor* d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:format
                                                                                         width:static_cast<NSUInteger>(width_)
                                                                                        height:static_cast<NSUInteger>(height_)
                                                                                     mipmapped:NO];
            d.textureType = count > 1 ? MTLTextureType2DMultisample : MTLTextureType2D;
            d.sampleCount = count;
            d.storageMode = MTLStorageModePrivate;
            d.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
            return [device_ newTextureWithDescriptor:d];
        };
        color_ = make(MTLPixelFormatRGBA8Unorm, samples_);
        resolve_ = samples_ > 1 ? make(MTLPixelFormatRGBA8Unorm, 1) : nil;
        depth_ = make(MTLPixelFormatDepth32Float_Stencil8, samples_);
        readback_ = [device_ newBufferWithLength:static_cast<NSUInteger>(width_) * static_cast<NSUInteger>(height_) * 4U
                                         options:MTLResourceStorageModeShared];
        return color_ != nil && depth_ != nil && readback_ != nil;
    }

    id<MTLTexture> texture(const ImagePreview* image, id<MTLCommandBuffer> commands) {
        if (image == nullptr || !image->available()) return nil;
        if (image->width > 16384U || image->height > 16384U) return nil;
        auto& entry = textures_[image];
        const auto signature = texel_signature(*image);
        if (entry.texture == nil || entry.data != image->rgba8.data() || entry.signature != signature) {
            MTLTextureDescriptor* d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                                                                         width:image->width
                                                                                        height:image->height
                                                                                     mipmapped:YES];
            d.usage = MTLTextureUsageShaderRead;
            entry.texture = [device_ newTextureWithDescriptor:d];
            [entry.texture replaceRegion:MTLRegionMake2D(0, 0, image->width, image->height)
                             mipmapLevel:0
                               withBytes:image->rgba8.data()
                             bytesPerRow:image->width * 4U];
            id<MTLBlitCommandEncoder> blit = [commands blitCommandEncoder];
            [blit generateMipmapsForTexture:entry.texture];
            [blit endEncoding];
            texture_bytes_ -= entry.bytes;
            entry.bytes = image->rgba8.size() * 4U / 3U;
            texture_bytes_ += entry.bytes;
            entry.data = image->rgba8.data();
            entry.signature = signature;
        }
        entry.last_frame = frame_number_;
        return entry.texture;
    }

    void trim_textures() {
        constexpr std::size_t kBudget = 1024U * 1024U * 1024U;
        for (auto it = textures_.begin(); it != textures_.end();) {
            const bool stale = frame_number_ - it->second.last_frame > 900U;
            const bool over = texture_bytes_ > kBudget && it->second.last_frame != frame_number_;
            if (stale || over) {
                texture_bytes_ -= it->second.bytes;
                it = textures_.erase(it);
            } else {
                ++it;
            }
        }
    }

    // Vertices for this frame: small ones inline, the rest in a buffer.
    void set_vertices(id<MTLRenderCommandEncoder> encoder, const void* data, std::size_t bytes) {
        if (bytes <= 4096U) {
            [encoder setVertexBytes:data length:bytes atIndex:0];
        } else {
            id<MTLBuffer> buffer = [device_ newBufferWithBytes:data length:bytes options:MTLResourceStorageModeShared];
            [encoder setVertexBuffer:buffer offset:0 atIndex:0];
        }
    }

    static Blend blend_for(GpuBatchKind kind, bool soft_alpha) {
        switch (kind) {
            case GpuBatchKind::model_additive: return Blend::add_src_alpha;
            case GpuBatchKind::room_additive: return Blend::add_one;
            case GpuBatchKind::model_subtractive: return Blend::sub_src_alpha;
            case GpuBatchKind::room_subtractive: return Blend::sub_one;
            default: return soft_alpha ? Blend::alpha : Blend::none;
        }
    }

    // A batch with the DMC3 pair Core chose for it (dmc3_draw); false when
    // the pair is not one this backend has, and the Reader's shader draws it.
    bool draw_dmc3(id<MTLRenderCommandEncoder> encoder, id<MTLCommandBuffer> commands, const GpuBatch& batch,
                   dmcresource::GpuPass pass, bool smooth, Blend blend, const GpuViewFrame& frame,
                   const std::array<float, 16>& view) {
        const auto d = dmcresource::dmc3_draw(batch, pass, frame);
        if (!d.drawn()) return false;
        const bool enhanced = frame.options.shading != 0;
        const char* vertex = enhanced ? dmc3_enhanced_function(d.vertex) : dmc3_function(d.vertex);
        const char* fragment = enhanced ? dmc3_enhanced_function(d.fragment) : dmc3_function(d.fragment);
        if (vertex == nullptr || fragment == nullptr) return false;
        id<MTLRenderPipelineState> state = pipeline(vertex, fragment, dmc3_layout_, blend, samples_);
        if (state == nil) return false;
        Dmc3Uniforms u{};
        u.view_projection = view_projection(view, frame.projection);
        u.light_directions = d.light_directions;
        u.light_colours = d.light_colours;
        u.part_colour = {d.part_colour, d.part_colour, d.part_colour, d.fog_amount};
        u.part_alpha = {d.part_alpha, 0.0F, 0.0F, 0.0F};
        u.light_x = d.light_x;
        u.light_y = d.light_y;
        u.light_z = d.light_z;
        u.light_falloff = d.light_falloff;
        u.light_colour = d.light_colour;
        u.ambient = {d.ambient[0], d.ambient[1], d.ambient[2], 0.0F};
        u.fog_colour = {d.fog_colour[0], d.fog_colour[1], d.fog_colour[2], 0.0F};
        u.fog_scroll = {frame.dmc3.fog_coeffs[0], frame.dmc3.fog_coeffs[1], 0.0F, 0.0F};
        if (batch.scroll_slot != dmcresource::kNoTextureSlot) {
            for (const auto& s : frame.room_scrolls) {
                if (s.slot == batch.scroll_slot) {
                    u.fog_scroll[2] = s.u;
                    u.fog_scroll[3] = s.v;
                    break;
                }
            }
        }
        u.alpha = {d.alpha_func == dmcresource::dmc3::AlphaFunc::off ? 0 : 1, static_cast<std::int32_t>(d.alpha_func),
                   0, 0};
        u.alpha_ref = {d.alpha_ref, 0.0F, 0.0F, 0.0F};
        if (enhanced) {
            using dmcresource::dmc3::Shader;
            u.camera = camera_position(view);
            const int kind = d.vertex == Shader::effect_model_lit_vert ? 1
                             : d.vertex == Shader::model_lit_vert   ? 0
                                                                    : 2;
            // Highlights on the model's own surfaces and the opaque stage, not
            // on what is added or subtracted over the scene.
            const bool spec = kind == 2 ? blend == Blend::none : batch.kind == dmcresource::GpuBatchKind::model;
            u.enhanced = {kind, spec ? 1 : 0, 0, 0};
        }
        id<MTLTexture> t = texture(batch.texture, commands);
        [encoder setRenderPipelineState:state];
        [encoder setVertexBytes:&u length:sizeof(u) atIndex:1];
        [encoder setFragmentBytes:&u length:sizeof(u) atIndex:0];
        [encoder setFragmentTexture:(t != nil ? t : blank_) atIndex:0];
        [encoder setFragmentSamplerState:sampler(smooth && !batch.nearest, frame.options) atIndex:0];
        [encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:batch.first vertexCount:batch.count];
        ++dmc3_batches_;
        return true;
    }

    void draw_batch(id<MTLRenderCommandEncoder> encoder, id<MTLCommandBuffer> commands, const GpuBatch& batch,
                    int mode, bool smooth, Blend blend, const GpuViewFrame& frame, MeshVertexUniforms& vu,
                    MeshFragmentUniforms& fu, dmcresource::GpuPass pass) {
        if (draw_dmc3(encoder, commands, batch, pass, smooth, blend, frame, vu.view)) return;
        ++reader_batches_;
        [encoder setRenderPipelineState:pipeline(false, blend, samples_)];

        id<MTLTexture> t = texture(batch.texture, commands);
        fu.textured = t != nil ? 1 : 0;
        fu.mode = mode;
        fu.colored = batch.colored ? 1 : 0;
        fu.translucent = batch.translucent ? 1 : 0;
        vu.light = {batch.light_base, batch.light_gain};
        vu.scroll = {0.0F, 0.0F};
        if (batch.scroll_slot != dmcresource::kNoTextureSlot) {
            for (const auto& s : frame.room_scrolls) {
                if (s.slot == batch.scroll_slot) {
                    vu.scroll = {s.u, s.v};
                    break;
                }
            }
        }
        [encoder setVertexBytes:&vu length:sizeof(vu) atIndex:1];
        [encoder setFragmentBytes:&fu length:sizeof(fu) atIndex:0];
        [encoder setFragmentTexture:(t != nil ? t : blank_) atIndex:0];
        [encoder setFragmentSamplerState:sampler(smooth && !batch.nearest, frame.options) atIndex:0];
        [encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:batch.first vertexCount:batch.count];
    }

    static std::vector<GpuVertex> plain(const std::vector<dmcresource::Vec3>& points) {
        std::vector<GpuVertex> out(points.size());
        for (std::size_t i = 0U; i < points.size(); ++i) {
            out[i].x = points[i].x;
            out[i].y = points[i].y;
            out[i].z = points[i].z;
        }
        return out;
    }

    void draw_screen(id<MTLRenderCommandEncoder> encoder, id<MTLCommandBuffer> commands,
                     const std::vector<GpuScreenVertex>& vertices, std::size_t first, std::size_t count,
                     const ImagePreview* image, Blend blend, const GpuViewFrame& frame, NSUInteger samples = 0U) {
        [encoder setRenderPipelineState:pipeline(true, blend, samples != 0U ? samples : samples_)];
        ScreenUniforms su{};
        su.size = {static_cast<float>(frame.width), static_cast<float>(frame.height)};
        su.depth = {frame.projection[2], frame.projection[3], frame.camera_distance, 0.0F};
        id<MTLTexture> t = texture(image, commands);
        su.textured = t != nil ? 1 : 0;
        [encoder setVertexBytes:&su length:sizeof(su) atIndex:1];
        [encoder setFragmentBytes:&su length:sizeof(su) atIndex:0];
        [encoder setFragmentTexture:(t != nil ? t : blank_) atIndex:0];
        [encoder setFragmentSamplerState:sampler(true, frame.options) atIndex:0];
        set_vertices(encoder, vertices.data() + first, count * sizeof(GpuScreenVertex));
        [encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:count];
    }

    // 8. The lines, crosses and labels (no depth test).
    void draw_overlay(id<MTLRenderCommandEncoder> encoder, id<MTLCommandBuffer> commands, const GpuViewFrame& frame,
                      float z, NSUInteger samples) {
    if (!frame.overlay.empty()) {
        std::vector<GpuScreenVertex> vertices = frame.overlay.triangles;
        for (auto& v : vertices) v.z = z;
        if (!vertices.empty()) draw_screen(encoder, commands, vertices, 0U, vertices.size(), nullptr, Blend::alpha, frame, samples);
        for (const auto& label : frame.overlay.labels) {
            const float x0 = label.x, y0 = label.y;
            const float x1 = x0 + static_cast<float>(label.image.width);
            const float y1 = y0 + static_cast<float>(label.image.height);
            const std::array<float, 4> white{255.0F, 255.0F, 255.0F, 255.0F};
            const std::vector<GpuScreenVertex> quad{{x0, y0, z, 0.0F, 0.0F, white}, {x1, y0, z, 1.0F, 0.0F, white},
                                                    {x1, y1, z, 1.0F, 1.0F, white}, {x0, y0, z, 0.0F, 0.0F, white},
                                                    {x1, y1, z, 1.0F, 1.0F, white}, {x0, y1, z, 0.0F, 1.0F, white}};
            draw_screen(encoder, commands, quad, 0U, quad.size(), &label.image, Blend::alpha, frame, samples);
        }
    }
    }

    bool draw_frame(const GpuViewFrame& frame, RgbaImage& image) {
        ++frame_number_;
        dmc3_batches_ = 0U;
        reader_batches_ = 0U;
        if (!ensure_target(frame.width, frame.height, frame.options.msaa_samples)) return false;
        id<MTLCommandBuffer> commands = [queue_ commandBuffer];

        // Textures upload (and their mipmaps build) before the pass begins.
        std::vector<const ImagePreview*> wanted;
        if (frame.room && (!room_cached_ || !(room_key_ == frame.room_key))) {
            const auto geometry = frame.build_room();
            room_buffer_ = geometry.vertices.empty()
                ? nil
                : [device_ newBufferWithBytes:geometry.vertices.data()
                                       length:geometry.vertices.size() * sizeof(GpuVertex)
                                      options:MTLResourceStorageModeShared];
            room_batches_ = geometry.batches;
            room_key_ = frame.room_key;
            room_cached_ = true;
        }
        if (frame.room) for (const auto& b : room_batches_) wanted.push_back(b.texture);
        for (const auto& b : frame.model.batches) wanted.push_back(b.texture);
        for (const auto& q : frame.effects) wanted.push_back(q.texture);
        for (const auto& l : frame.overlay.labels) wanted.push_back(&l.image);
        for (const auto* w : wanted) (void)texture(w, commands);

        MTLRenderPassDescriptor* pass = [MTLRenderPassDescriptor renderPassDescriptor];
        pass.colorAttachments[0].texture = color_;
        pass.colorAttachments[0].loadAction = MTLLoadActionClear;
        pass.colorAttachments[0].clearColor = MTLClearColorMake(frame.background[0] / 255.0, frame.background[1] / 255.0,
                                                                frame.background[2] / 255.0, 1.0);
        if (samples_ > 1) {
            pass.colorAttachments[0].resolveTexture = resolve_;
            pass.colorAttachments[0].storeAction = MTLStoreActionMultisampleResolve;
        } else {
            pass.colorAttachments[0].storeAction = MTLStoreActionStore;
        }
        pass.depthAttachment.texture = depth_;
        pass.depthAttachment.loadAction = MTLLoadActionClear;
        pass.depthAttachment.clearDepth = 1.0;
        pass.depthAttachment.storeAction = MTLStoreActionDontCare;
        // The development reads the depth: kept (resolved from sample 0).
        const bool developing = frame.post != nullptr && frame.post->enabled &&
                                static_cast<long long>(frame.width) * frame.height <= 4096LL * 4096LL &&
                                ensure_post_targets();
        if (developing) {
            if (samples_ > 1) {
                pass.depthAttachment.resolveTexture = depth_single_;
                pass.depthAttachment.depthResolveFilter = MTLMultisampleDepthResolveFilterSample0;
                pass.depthAttachment.storeAction = MTLStoreActionMultisampleResolve;
            } else {
                pass.depthAttachment.storeAction = MTLStoreActionStore;
            }
        }
        pass.stencilAttachment.texture = depth_;
        pass.stencilAttachment.loadAction = MTLLoadActionClear;
        pass.stencilAttachment.clearStencil = 0;
        pass.stencilAttachment.storeAction = MTLStoreActionDontCare;

        id<MTLRenderCommandEncoder> encoder = [commands renderCommandEncoderWithDescriptor:pass];
        [encoder setViewport:MTLViewport{0.0, 0.0, static_cast<double>(frame.width),
                                         static_cast<double>(frame.height), 0.0, 1.0}];
        [encoder setCullMode:MTLCullModeNone];

        MeshVertexUniforms vu{};
        vu.proj = frame.projection;
        vu.cd = frame.camera_distance;
        MeshFragmentUniforms fu{};
        fu.radius = std::max(frame.radius, 1.0e-4F);
        fu.cd = frame.camera_distance;
        const auto& fog = frame.fog;
        fu.fog = {fog.enabled ? 1.0F : 0.0F, fog.start, fog.end, 0.0F};
        fu.fog_near = fog.near_colour;
        fu.fog_far = fog.far_colour;

        // 1. Opaque room texels.
        if (frame.room && room_buffer_ != nil) {
            [encoder setVertexBuffer:room_buffer_ offset:0 atIndex:0];
            [encoder setDepthStencilState:depth_state(1, true, false)];
            vu.view = frame.room_view;
            vu.room = 1;
            for (const auto& batch : room_batches_) {
                if (batch.kind != GpuBatchKind::room) continue;
                draw_batch(encoder, commands, batch, 2, frame.smooth_room, Blend::none, frame, vu, fu,
                           dmcresource::GpuPass::room_opaque);
            }
        }

        // 2. Plain floor.
        vu.view = frame.model_view;
        vu.room = 0;
        vu.light = {1.0F, 0.0F};
        vu.scroll = {0.0F, 0.0F};
        if (!frame.floor.empty()) {
            const auto floor = plain(frame.floor);
            set_vertices(encoder, floor.data(), floor.size() * sizeof(GpuVertex));
            [encoder setRenderPipelineState:pipeline(false, Blend::none, samples_)];
            [encoder setDepthStencilState:depth_state(1, true, false)];
            fu.mode = 5;
            fu.textured = 0;
            fu.solid = {frame.floor_rgb[0] / 255.0F, frame.floor_rgb[1] / 255.0F, frame.floor_rgb[2] / 255.0F, 1.0F};
            [encoder setVertexBytes:&vu length:sizeof(vu) atIndex:1];
            [encoder setFragmentBytes:&fu length:sizeof(fu) atIndex:0];
            [encoder setFragmentTexture:blank_ atIndex:0];
            [encoder setFragmentSamplerState:sampler(true, frame.options) atIndex:0];
            [encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:floor.size()];
        }

        // 3. The model: blended-over batches writing depth, then the added /
        // subtracted ones on top without it.
        if (!frame.model.vertices.empty()) {
            set_vertices(encoder, frame.model.vertices.data(), frame.model.vertices.size() * sizeof(GpuVertex));
            vu.view = frame.model_view;
            vu.room = 0;
            for (const auto& batch : frame.model.batches) {
                const bool layered = batch.kind != GpuBatchKind::model;
                [encoder setDepthStencilState:depth_state(1, !layered, false)];
                draw_batch(encoder, commands, batch, layered ? 1 : 0, frame.smooth_model, blend_for(batch.kind, !layered),
                           frame, vu, fu, dmcresource::GpuPass::model);
            }
        }

        // 4. Soft room texels, then light shafts.
        if (frame.room && room_buffer_ != nil) {
            [encoder setVertexBuffer:room_buffer_ offset:0 atIndex:0];
            [encoder setDepthStencilState:depth_state(1, false, false)];
            vu.view = frame.room_view;
            vu.room = 1;
            for (const auto& batch : room_batches_) {
                if (batch.kind != GpuBatchKind::room || !batch.translucent) continue;
                draw_batch(encoder, commands, batch, 3, frame.smooth_room, blend_for(batch.kind, true), frame, vu, fu,
                           dmcresource::GpuPass::room_soft);
            }
            for (const auto& batch : room_batches_) {
                if (batch.kind == GpuBatchKind::room) continue;
                draw_batch(encoder, commands, batch, 4, frame.smooth_room, blend_for(batch.kind, true), frame, vu, fu,
                           dmcresource::GpuPass::room_layered);
            }
        }

        // 5. Shadow footprint: darkens what is visible there once per pixel.
        if (!frame.shadow.empty()) {
            const auto shadow = plain(frame.shadow);
            set_vertices(encoder, shadow.data(), shadow.size() * sizeof(GpuVertex));
            vu.view = frame.model_view;
            vu.room = 0;
            vu.light = {1.0F, 0.0F};
            vu.scroll = {0.0F, 0.0F};
            vu.depth_bias = frame.radius * 0.01F;
            fu.mode = 6;
            fu.textured = 0;
            [encoder setRenderPipelineState:pipeline(false, Blend::multiply, samples_)];
            [encoder setDepthStencilState:depth_state(2, false, true)];
            [encoder setStencilReferenceValue:0];
            [encoder setVertexBytes:&vu length:sizeof(vu) atIndex:1];
            [encoder setFragmentBytes:&fu length:sizeof(fu) atIndex:0];
            [encoder setFragmentTexture:blank_ atIndex:0];
            [encoder setFragmentSamplerState:sampler(true, frame.options) atIndex:0];
            [encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:shadow.size()];
            vu.depth_bias = 0.0F;
        }

        // 6. Effect quads (image space).
        if (!frame.effects.empty()) {
            std::vector<GpuScreenVertex> vertices;
            vertices.reserve(frame.effects.size() * 6U);
            for (const auto& quad : frame.effects) {
                for (const int k : {0, 1, 2, 0, 2, 3}) vertices.push_back(quad.vertices[static_cast<std::size_t>(k)]);
            }
            [encoder setDepthStencilState:depth_state(1, false, false)];
            for (std::size_t i = 0U; i < frame.effects.size(); ++i) {
                const auto& quad = frame.effects[i];
                draw_screen(encoder, commands, vertices, i * 6U, 6U, quad.texture,
                            quad.additive ? Blend::add_src_alpha : Blend::alpha, frame);
            }
        }

        // 7. A cutscene's fade, then 8. the overlays (no depth test).
        const float z = 0.5F * (frame.near_z + frame.far_z) - frame.camera_distance;
        const float w = static_cast<float>(frame.width);
        const float h = static_cast<float>(frame.height);
        [encoder setDepthStencilState:depth_state(0, false, false)];
        if (frame.fade[3] > 0.0F) {
            const std::array<float, 4> colour{frame.fade[0], frame.fade[1], frame.fade[2],
                                              std::clamp(frame.fade[3], 0.0F, 1.0F) * 255.0F};
            const std::vector<GpuScreenVertex> quad{{0.0F, 0.0F, z, 0.0F, 0.0F, colour}, {w, 0.0F, z, 0.0F, 0.0F, colour},
                                                    {w, h, z, 0.0F, 0.0F, colour},       {0.0F, 0.0F, z, 0.0F, 0.0F, colour},
                                                    {w, h, z, 0.0F, 0.0F, colour},       {0.0F, h, z, 0.0F, 0.0F, colour}};
            draw_screen(encoder, commands, quad, 0U, quad.size(), nullptr, Blend::alpha, frame);
        }
        if (!developing) draw_overlay(encoder, commands, frame, z, 0U);
        [encoder endEncoding];

        // The photo camera's development on the frame and its depth, then the
        // overlays over the developed frame.
        id<MTLTexture> finished = samples_ > 1 ? resolve_ : color_;
        if (developing) {
            id<MTLTexture> developed = develop(commands, frame, *frame.post);
            frame.post->applied = developed != nil;
            if (developed != nil) finished = developed;
            if (!frame.overlay.empty()) overlay_pass(commands, frame, finished, z);
        }

        publish_stats();
        // Straight to the window unless the CPU still has to develop it.
        const bool undeveloped = frame.post != nullptr && frame.post->enabled && !frame.post->applied;
        if (frame.present && layer_ != nil && !undeveloped && present(commands, frame, finished)) {
            frame.presented = true;
            trim_textures();
            return true;
        }

        // Read back: Metal row 0 is image row 0, as RgbaImage stores them.
        id<MTLBlitCommandEncoder> blit = [commands blitCommandEncoder];
        const NSUInteger row = static_cast<NSUInteger>(frame.width) * 4U;
        [blit copyFromTexture:finished
                         sourceSlice:0
                         sourceLevel:0
                        sourceOrigin:MTLOriginMake(0, 0, 0)
                          sourceSize:MTLSizeMake(static_cast<NSUInteger>(frame.width),
                                                 static_cast<NSUInteger>(frame.height), 1)
                            toBuffer:readback_
                   destinationOffset:0
              destinationBytesPerRow:row
            destinationBytesPerImage:row * static_cast<NSUInteger>(frame.height)];
        [blit endEncoding];
        [commands commit];
        [commands waitUntilCompleted];
        if (commands.status != MTLCommandBufferStatusCompleted) return false;

        image.width = frame.width;
        image.height = frame.height;
        image.pixels.resize(row * static_cast<std::size_t>(frame.height));
        std::memcpy(image.pixels.data(), [readback_ contents], image.pixels.size());
        trim_textures();
        return true;
    }

    void publish_stats() {
        dmcresource::GpuShaderStats stats;
        stats.compiled = 6;
        stats.dmc3_batches = dmc3_batches_;
        stats.reader_batches = reader_batches_;
        dmcresource::set_gpu_shader_stats(std::move(stats));
    }

    // Single-sample targets of the development: the depth it reads (the
    // resolved one with MSAA), two colour targets, the auto focus grid.
    bool ensure_post_targets() {
        if (ping_ != nil && post_width_ == width_ && post_height_ == height_ && post_samples_ == samples_) return true;
        const auto make = [this](MTLPixelFormat format, int w, int h) {
            MTLTextureDescriptor* d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:format
                                                                                         width:static_cast<NSUInteger>(w)
                                                                                        height:static_cast<NSUInteger>(h)
                                                                                     mipmapped:NO];
            d.storageMode = MTLStorageModePrivate;
            d.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
            return [device_ newTextureWithDescriptor:d];
        };
        ping_ = make(MTLPixelFormatRGBA8Unorm, width_, height_);
        pong_ = make(MTLPixelFormatRGBA8Unorm, width_, height_);
        depth_single_ = samples_ > 1 ? make(MTLPixelFormatDepth32Float_Stencil8, width_, height_) : nil;
        if (focus_ == nil) focus_ = make(MTLPixelFormatRGBA8Unorm, 16, 16);
        if (focus_buffer_ == nil) focus_buffer_ = [device_ newBufferWithLength:16U * 16U * 4U options:MTLResourceStorageModeShared];
        post_width_ = width_;
        post_height_ = height_;
        post_samples_ = samples_;
        return ping_ != nil && pong_ != nil && focus_ != nil && focus_buffer_ != nil && (samples_ == 1 || depth_single_ != nil);
    }

    id<MTLRenderPipelineState> post_pipeline(const char* fragment) {
        auto& slot = post_pipelines_[fragment];
        if (slot != nil) return slot;
        MTLRenderPipelineDescriptor* p = [[MTLRenderPipelineDescriptor alloc] init];
        p.vertexFunction = function("present_vertex");
        p.fragmentFunction = function(fragment);
        if (p.fragmentFunction == nil) return nil;
        p.colorAttachments[0].pixelFormat = MTLPixelFormatRGBA8Unorm;
        NSError* error = nil;
        slot = [device_ newRenderPipelineStateWithDescriptor:p error:&error];
        return slot;
    }

    // One full-screen pass of `fragment` into `target`.
    template <typename Uniforms>
    bool post_pass(id<MTLCommandBuffer> commands, id<MTLTexture> target, const char* fragment, const Uniforms& u,
                   std::initializer_list<id<MTLTexture>> textures, const void* extra = nullptr,
                   std::size_t extra_size = 0U) {
        id<MTLRenderPipelineState> state = post_pipeline(fragment);
        if (state == nil) return false;
        MTLRenderPassDescriptor* pass = [MTLRenderPassDescriptor renderPassDescriptor];
        pass.colorAttachments[0].texture = target;
        pass.colorAttachments[0].loadAction = MTLLoadActionDontCare;
        pass.colorAttachments[0].storeAction = MTLStoreActionStore;
        id<MTLRenderCommandEncoder> encoder = [commands renderCommandEncoderWithDescriptor:pass];
        [encoder setRenderPipelineState:state];
        [encoder setFragmentBytes:&u length:sizeof(u) atIndex:0];
        if (extra != nullptr) [encoder setFragmentBytes:extra length:extra_size atIndex:1];
        NSUInteger index = 0U;
        for (id<MTLTexture> t : textures) [encoder setFragmentTexture:t atIndex:index++];
        [encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
        [encoder endEncoding];
        return true;
    }

    // The development, as the OpenGL ES backend's: the auto focus when asked
    // for (its grid read back for the median: the frame so far is committed
    // and `commands` is a new buffer after it), the depth of field, the lens
    // / polarizer / look, sharpening. The developed texture, or nil.
    id<MTLTexture> develop(__strong id<MTLCommandBuffer>& commands, const GpuViewFrame& frame, const dmcresource::CameraPost& post) {
        const int w = frame.width;
        const int h = frame.height;
        id<MTLTexture> depth = samples_ > 1 ? depth_single_ : depth_;
        id<MTLTexture> source = samples_ > 1 ? resolve_ : color_;
        const std::array<float, 2> proj{frame.projection[2], frame.projection[3]};
        if (post.depth_of_field) {
            float focus_mm = post.focus_mm;
            if (focus_mm == 0.0F) {
                // The auto focus: the median distance in the middle.
                const FocusUniforms fu{{w, h}, proj, post.auto_focus_half};
                if (!post_pass(commands, focus_, "focus_fragment", fu, {depth})) return nil;
                id<MTLBlitCommandEncoder> blit = [commands blitCommandEncoder];
                [blit copyFromTexture:focus_ sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0, 0, 0)
                           sourceSize:MTLSizeMake(16, 16, 1) toBuffer:focus_buffer_ destinationOffset:0
               destinationBytesPerRow:64 destinationBytesPerImage:1024];
                [blit endEncoding];
                [commands commit];
                [commands waitUntilCompleted];
                if (commands.status != MTLCommandBufferStatusCompleted) return nil;
                commands = [queue_ commandBuffer];
                const auto* packed = static_cast<const std::uint8_t*>([focus_buffer_ contents]);
                std::vector<float> near;
                for (std::size_t i = 0; i < 1024U; i += 4U) {
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
            DofUniforms du{};
            du.size = {w, h};
            du.proj = proj;
            du.focus_mm = focus_mm;
            du.f = post.focal_mm;
            du.k = post.coc_k;
            du.unit = post.unit_mm;
            du.px_per_mm = post.px_per_mm;
            du.max_r = post.max_radius;
            du.taps = std::clamp(post.taps, 1, 128);
            du.bright = post.bright_bokeh ? 1 : 0;
            du.tap = post.tap_xy;
            if (!post_pass(commands, ping_, "dof_fragment", du, {source, depth})) return nil;
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
        LookUniforms lu{};
        lu.size = {w, h};
        lu.k = post.distortion_k;
        lu.fringe = post.fringe;
        lu.pol = post.polarizer ? 1 : 0;
        lu.pol_strength = post.polarizer_strength;
        lu.ring = post.polarizer_ring;
        lu.sat = post.saturation;
        lu.tint = {post.tint[0], post.tint[1], post.tint[2], 0.0F};
        lu.cinema = post.cinema ? 1 : 0;
        lu.vignette = post.vignette;
        lu.sigma = post.noise_sigma;
        lu.grain = std::max(1, post.grain);
        lu.chroma = post.chroma_noise ? 1 : 0;
        lu.seed = post.seed;
        id<MTLTexture> result = source == ping_ ? pong_ : ping_;
        if (!post_pass(commands, result, "look_fragment", lu, {source}, curves.data(), curves.size())) return nil;

        if (post.sharpen > 0.0F) {
            const SharpenUniforms su{{w, h}, post.sharpen};
            id<MTLTexture> sharpened = result == ping_ ? pong_ : ping_;
            if (!post_pass(commands, sharpened, "sharpen_fragment", su, {result})) return nil;
            result = sharpened;
        }
        return result;
    }

    // The overlays over a single-sample texture (the developed frame).
    void overlay_pass(id<MTLCommandBuffer> commands, const GpuViewFrame& frame, id<MTLTexture> target, float z) {
        MTLRenderPassDescriptor* pass = [MTLRenderPassDescriptor renderPassDescriptor];
        pass.colorAttachments[0].texture = target;
        pass.colorAttachments[0].loadAction = MTLLoadActionLoad;
        pass.colorAttachments[0].storeAction = MTLStoreActionStore;
        id<MTLTexture> depth = samples_ > 1 ? depth_single_ : depth_;
        pass.depthAttachment.texture = depth;
        pass.depthAttachment.loadAction = MTLLoadActionClear;
        pass.depthAttachment.clearDepth = 1.0;
        pass.depthAttachment.storeAction = MTLStoreActionDontCare;
        pass.stencilAttachment.texture = depth;
        pass.stencilAttachment.loadAction = MTLLoadActionClear;
        pass.stencilAttachment.storeAction = MTLStoreActionDontCare;
        id<MTLRenderCommandEncoder> encoder = [commands renderCommandEncoderWithDescriptor:pass];
        [encoder setViewport:MTLViewport{0.0, 0.0, static_cast<double>(frame.width),
                                         static_cast<double>(frame.height), 0.0, 1.0}];
        [encoder setCullMode:MTLCullModeNone];
        [encoder setDepthStencilState:depth_state(0, false, false)];
        draw_overlay(encoder, commands, frame, z, 1U);
        [encoder endEncoding];
    }

    // Copies the finished frame onto the layer's next drawable and presents
    // it with this command buffer; false (nothing committed) when the layer
    // has no drawable to give.
    bool present(id<MTLCommandBuffer> commands, const GpuViewFrame& frame, id<MTLTexture> finished) {
        const CGSize size = CGSizeMake(frame.width, frame.height);
        if (!CGSizeEqualToSize(layer_.drawableSize, size)) {
            [CATransaction begin];
            [CATransaction setDisableActions:YES];
            layer_.drawableSize = size;
            [CATransaction commit];
        }
        if (present_state_ == nil) {
            MTLRenderPipelineDescriptor* p = [[MTLRenderPipelineDescriptor alloc] init];
            p.vertexFunction = function("present_vertex");
            p.fragmentFunction = function("present_fragment");
            p.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
            NSError* error = nil;
            present_state_ = [device_ newRenderPipelineStateWithDescriptor:p error:&error];
            if (present_state_ == nil) return false;
        }
        id<CAMetalDrawable> drawable = [layer_ nextDrawable];
        if (drawable == nil) return false;
        MTLRenderPassDescriptor* pass = [MTLRenderPassDescriptor renderPassDescriptor];
        pass.colorAttachments[0].texture = drawable.texture;
        pass.colorAttachments[0].loadAction = MTLLoadActionDontCare;
        pass.colorAttachments[0].storeAction = MTLStoreActionStore;
        id<MTLRenderCommandEncoder> encoder = [commands renderCommandEncoderWithDescriptor:pass];
        [encoder setRenderPipelineState:present_state_];
        [encoder setFragmentTexture:finished atIndex:0];
        [encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
        [encoder endEncoding];
        [commands presentDrawable:drawable];
        [commands commit];
        return true;
    }

    std::mutex mutex_;
    bool failed_{};
    unsigned failures_{};
    std::uint64_t frame_number_{};
    id<MTLDevice> device_{};
    id<MTLCommandQueue> queue_{};
    id<MTLLibrary> library_{};
    id<MTLFunction> mesh_vertex_{}, mesh_fragment_{}, screen_vertex_{}, screen_fragment_{};
    MTLVertexDescriptor* mesh_layout_{};
    MTLVertexDescriptor* screen_layout_{};
    id<MTLTexture> blank_{};
    std::map<std::string, id<MTLRenderPipelineState>> pipelines_;
    std::map<std::string, id<MTLFunction>> functions_;
    MTLVertexDescriptor* dmc3_layout_{};
    std::uint32_t dmc3_batches_{};
    std::uint32_t reader_batches_{};
    std::map<int, id<MTLDepthStencilState>> depth_states_;
    std::map<int, id<MTLSamplerState>> samplers_;
    id<MTLTexture> color_{}, resolve_{}, depth_{};
    id<MTLBuffer> readback_{};
    CAMetalLayer* layer_{};
    id<MTLRenderPipelineState> present_state_{};
    id<MTLTexture> ping_{}, pong_{}, depth_single_{}, focus_{};
    id<MTLBuffer> focus_buffer_{};
    int post_width_{}, post_height_{};
    NSUInteger post_samples_{};
    std::map<std::string, id<MTLRenderPipelineState>> post_pipelines_;
    int width_{}, height_{};
    NSUInteger samples_{1};
    std::unordered_map<const ImagePreview*, Texture> textures_;
    std::size_t texture_bytes_{};
    id<MTLBuffer> room_buffer_{};
    std::vector<GpuBatch> room_batches_;
    dmcresource::GpuRoomKey room_key_{};
    bool room_cached_{};
};

}  // namespace

// Registers the Metal backend for render_view (the C API calls it once).
extern "C" void dmc_install_metal_view_backend() {
    static MetalViewBackend backend;
    dmcresource::set_gpu_view_backend(&backend);
}
