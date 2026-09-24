#include "dmcresource/view_renderer.h"

#include <algorithm>
#include <atomic>
#include <thread>
#include <unordered_map>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>
#include <vector>

namespace dmcresource {
namespace {

struct P3 { float x, y, z; };
struct P2 { float x, y, z; };

float edge(const P2& a, const P2& b, float x, float y) {
    return (x - a.x) * (b.y - a.y) - (y - a.y) * (b.x - a.x);
}

// World to camera orientation. DMC3 data are right-handed (the right hand,
// body joint 9 of the sword state-2 record, is on -X of a model facing +Z)
// while this camera looks down +Z with +X to the right, so X is mirrored
// first; without it every model and stage showed as its mirror image.
P3 rotate(const Vec3& world, float yaw, float pitch) {
    const Vec3 v{-world.x, world.y, world.z};
    const float cy = std::cos(yaw), sy = std::sin(yaw);
    const float cp = std::cos(pitch), sp = std::sin(pitch);
    const float x1 = cy * v.x + sy * v.z;
    const float z1 = -sy * v.x + cy * v.z;
    return {x1, cp * v.y - sp * z1, sp * v.y + cp * z1};
}

// Mesh-centered pinhole camera framing, factored out of render_view so
// project_hierarchy_points() can reproduce the exact same screen position for
// a hierarchy joint marker that render_view itself drew -- any drift between
// two independent copies of this math would silently break hover hit-testing
// against what's actually on screen.
struct CameraFrame {
    Vec3 center{};
    float radius{1.0e-4F};
    float camera_distance{};
    float focal_px{};
    float pan_x{};  // camera-plane shift in world units
    float pan_y{};
};

constexpr float kHalfFovRadians = 0.5F;  // ~29 deg half-FOV; moderate, not fisheye.

CameraFrame compute_camera_frame(std::span<const Vec3> vertices, int width, int height) {
    CameraFrame frame;
    if (vertices.empty()) return frame;

    for (const auto& v : vertices) {
        frame.center.x += v.x;
        frame.center.y += v.y;
        frame.center.z += v.z;
    }
    const float inv_n = 1.0F / static_cast<float>(vertices.size());
    frame.center.x *= inv_n;
    frame.center.y *= inv_n;
    frame.center.z *= inv_n;

    for (const auto& v : vertices) {
        const float dx = v.x - frame.center.x;
        const float dy = v.y - frame.center.y;
        const float dz = v.z - frame.center.z;
        frame.radius = std::max(frame.radius, std::sqrt(dx * dx + dy * dy + dz * dz));
    }

    // Pinhole perspective camera, framed so the model's bounding sphere fills
    // most of the shorter image axis at zoom == 1 (see render_view for the
    // perspective-divide rationale).
    frame.camera_distance = 1.3F * frame.radius / std::sin(kHalfFovRadians);
    frame.focal_px = 0.5F *
        static_cast<float>(std::min(width, height)) / std::tan(kHalfFovRadians);
    return frame;
}

P2 project_in_frame(const CameraFrame& frame, const Vec3& world, float yaw, float pitch,
                    float zoom, int width, int height) {
    const Vec3 local{
        world.x - frame.center.x, world.y - frame.center.y, world.z - frame.center.z};
    const auto r = rotate(local, yaw, pitch);
    const float z_cam = r.z + frame.camera_distance;
    const float inv_z = 1.0F / std::max(z_cam, 1.0e-3F);
    return {static_cast<float>(width) * 0.5F + zoom * frame.focal_px * (r.x - frame.pan_x) * inv_z,
           static_cast<float>(height) * 0.5F - zoom * frame.focal_px * (r.y - frame.pan_y) * inv_z, r.z};
}

// The camera of a view: framed on the rest pose (or the mesh), then moved by
// the follow shift and the gesture pan.
CameraFrame view_frame(const Mesh& mesh, const ViewState& view, int width, int height) {
    auto frame = compute_camera_frame(
        view.framing_vertices.empty() ? std::span<const Vec3>{mesh.vertices} : view.framing_vertices,
        width, height);
    frame.center.x += view.frame_shift.x;
    frame.center.y += view.frame_shift.y;
    frame.center.z += view.frame_shift.z;
    frame.pan_x = view.pan_x * frame.radius;
    frame.pan_y = view.pan_y * frame.radius;
    return frame;
}

// Room vertex / normal placement: turned by room_yaw about room_pivot, then
// moved by room_offset.
Vec3 room_place(const ViewState& view, const Vec3& v) {
    const float c = std::cos(view.room_yaw), s = std::sin(view.room_yaw);
    const float x = v.x - view.room_pivot.x, z = v.z - view.room_pivot.z;
    return {c * x + s * z + view.room_pivot.x + view.room_offset.x, v.y + view.room_offset.y,
            -s * x + c * z + view.room_pivot.z + view.room_offset.z};
}

Vec3 room_turn(const ViewState& view, const Vec3& n) {
    const float c = std::cos(view.room_yaw), s = std::sin(view.room_yaw);
    return {c * n.x + s * n.z, n.y, -s * n.x + c * n.z};
}

void put_pixel(RgbaImage& image, int x, int y, std::uint8_t shade) {
    if (x < 0 || y < 0 || x >= image.width || y >= image.height) return;
    const auto o = static_cast<std::size_t>(y * image.width + x) * 4U;
    image.pixels[o + 0U] = shade;
    image.pixels[o + 1U] = shade;
    image.pixels[o + 2U] = static_cast<std::uint8_t>(std::min(255, shade + 10));
    image.pixels[o + 3U] = 255U;
}

void put_rgba(RgbaImage& image, int x, int y,
              std::uint8_t r, std::uint8_t g,
              std::uint8_t b, std::uint8_t a) {
    if (x < 0 || y < 0 || x >= image.width || y >= image.height || a == 0U) return;
    const auto o = static_cast<std::size_t>(y * image.width + x) * 4U;
    if (a == 255U) {
        image.pixels[o + 0U] = r;
        image.pixels[o + 1U] = g;
        image.pixels[o + 2U] = b;
        image.pixels[o + 3U] = 255U;
        return;
    }
    const std::uint32_t alpha = a;
    const std::uint32_t inverse = 255U - alpha;
    image.pixels[o + 0U] = static_cast<std::uint8_t>(
        (static_cast<std::uint32_t>(r) * alpha +
         static_cast<std::uint32_t>(image.pixels[o + 0U]) * inverse) / 255U);
    image.pixels[o + 1U] = static_cast<std::uint8_t>(
        (static_cast<std::uint32_t>(g) * alpha +
         static_cast<std::uint32_t>(image.pixels[o + 1U]) * inverse) / 255U);
    image.pixels[o + 2U] = static_cast<std::uint8_t>(
        (static_cast<std::uint32_t>(b) * alpha +
         static_cast<std::uint32_t>(image.pixels[o + 2U]) * inverse) / 255U);
    image.pixels[o + 3U] = 255U;
}

// GS ALPHA modes from the object flags (table 0x1405D0550): 2 adds the
// source weighted by its alpha, 3 subtracts it; others blend normally.
void blend_rgba(RgbaImage& image, int x, int y, std::uint8_t r, std::uint8_t g,
                std::uint8_t b, std::uint8_t a, std::uint8_t mode) {
    if (mode != 2U && mode != 3U) {
        put_rgba(image, x, y, r, g, b, a);
        return;
    }
    if (x < 0 || y < 0 || x >= image.width || y >= image.height || a == 0U) return;
    const auto o = static_cast<std::size_t>(y * image.width + x) * 4U;
    const std::uint8_t src[3] = {r, g, b};
    for (std::size_t k = 0U; k < 3U; ++k) {
        const int weighted = static_cast<int>(src[k]) * static_cast<int>(a) / 255;
        const int value = mode == 2U ? image.pixels[o + k] + weighted
                                     : image.pixels[o + k] - weighted;
        image.pixels[o + k] = static_cast<std::uint8_t>(std::clamp(value, 0, 255));
    }
}

void line(RgbaImage& image, P2 a, P2 b, std::uint8_t shade = 235) {
    int x0 = static_cast<int>(std::lround(a.x));
    int y0 = static_cast<int>(std::lround(a.y));
    const int x1 = static_cast<int>(std::lround(b.x));
    const int y1 = static_cast<int>(std::lround(b.y));
    const int dx = std::abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    const int dy = -std::abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        put_pixel(image, x0, y0, shade);
        if (x0 == x1 && y0 == y1) break;
        const int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

void line_rgba(RgbaImage& image, P2 a, P2 b, std::uint8_t r, std::uint8_t g, std::uint8_t bl) {
    const float limit = 4.0F * static_cast<float>(std::max(image.width, image.height));
    if (!std::isfinite(a.x) || !std::isfinite(a.y) || !std::isfinite(b.x) || !std::isfinite(b.y) ||
        std::fabs(a.x) > limit || std::fabs(a.y) > limit || std::fabs(b.x) > limit || std::fabs(b.y) > limit) {
        return;
    }
    int x0 = static_cast<int>(std::lround(a.x));
    int y0 = static_cast<int>(std::lround(a.y));
    const int x1 = static_cast<int>(std::lround(b.x));
    const int y1 = static_cast<int>(std::lround(b.y));
    const int dx = std::abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    const int dy = -std::abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        put_rgba(image, x0, y0, r, g, bl, 255U);
        if (x0 == x1 && y0 == y1) break;
        const int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

void marker(RgbaImage& image, P2 point, std::uint8_t shade) {
    const int x = static_cast<int>(std::lround(point.x));
    const int y = static_cast<int>(std::lround(point.y));
    for (int d = -2; d <= 2; ++d) {
        put_pixel(image, x + d, y, shade);
        put_pixel(image, x, y + d, shade);
    }
}

[[nodiscard]] float repeat_unit(float value) noexcept {
    if (!std::isfinite(value)) return 0.0F;
    const float repeated = value - std::floor(value);
    return repeated < 0.0F ? repeated + 1.0F : repeated;
}

[[nodiscard]] bool sample_texture(const ImagePreview& texture,
                                  float u, float v,
                                  std::uint8_t* r,
                                  std::uint8_t* g,
                                  std::uint8_t* b,
                                  std::uint8_t* a) noexcept {
    if (!texture.available() || r == nullptr || g == nullptr ||
        b == nullptr || a == nullptr) return false;

    const float uu = repeat_unit(u);
    const float vv = repeat_unit(v);
    const auto x = std::min<std::uint32_t>(
        texture.width - 1U,
        static_cast<std::uint32_t>(uu * static_cast<float>(texture.width)));
    const auto y = std::min<std::uint32_t>(
        texture.height - 1U,
        static_cast<std::uint32_t>(vv * static_cast<float>(texture.height)));
    const auto offset =
        (static_cast<std::size_t>(y) * texture.width + x) * 4U;
    if (offset > texture.rgba8.size() || texture.rgba8.size() - offset < 4U) {
        return false;
    }
    *r = texture.rgba8[offset + 0U];
    *g = texture.rgba8[offset + 1U];
    *b = texture.rgba8[offset + 2U];
    *a = texture.rgba8[offset + 3U];
    return true;
}

void draw_uv_layout(std::span<const Vec2> coordinates,
                    std::span<const std::uint32_t> indices,
                    const ViewState& view, RgbaImage* image) {
    if (image == nullptr || coordinates.empty() || indices.size() < 3U) return;

    float min_u = 0.0F;
    float min_v = 0.0F;
    float max_u = 1.0F;
    float max_v = 1.0F;
    for (const auto index : indices) {
        if (index >= coordinates.size()) return;
        const auto& uv = coordinates[index];
        if (!std::isfinite(uv.u) || !std::isfinite(uv.v)) return;
        min_u = std::min(min_u, uv.u);
        min_v = std::min(min_v, uv.v);
        max_u = std::max(max_u, uv.u);
        max_v = std::max(max_v, uv.v);
    }

    const float span_u = std::max(1.0e-5F, max_u - min_u);
    const float span_v = std::max(1.0e-5F, max_v - min_v);
    const float padding = 0.08F * static_cast<float>(
        std::min(image->width, image->height));
    const float usable_w = std::max(
        1.0F, static_cast<float>(image->width) - 2.0F * padding);
    const float usable_h = std::max(
        1.0F, static_cast<float>(image->height) - 2.0F * padding);
    const float zoom = std::clamp(view.zoom, 0.15F, 8.0F);
    const float scale = std::min(usable_w / span_u, usable_h / span_v) * zoom;
    const float center_u = (min_u + max_u) * 0.5F;
    const float center_v = (min_v + max_v) * 0.5F;

    const auto map_uv = [&](float u, float v) -> P2 {
        return {
            static_cast<float>(image->width) * 0.5F + (u - center_u) * scale,
            static_cast<float>(image->height) * 0.5F - (v - center_v) * scale,
            0.0F,
        };
    };

    const P2 uv00 = map_uv(0.0F, 0.0F);
    const P2 uv10 = map_uv(1.0F, 0.0F);
    const P2 uv11 = map_uv(1.0F, 1.0F);
    const P2 uv01 = map_uv(0.0F, 1.0F);
    line(*image, uv00, uv10, 80);
    line(*image, uv10, uv11, 80);
    line(*image, uv11, uv01, 80);
    line(*image, uv01, uv00, 80);

    for (std::size_t t = 0U; t + 2U < indices.size(); t += 3U) {
        const auto a = coordinates[indices[t]];
        const auto b = coordinates[indices[t + 1U]];
        const auto c = coordinates[indices[t + 2U]];
        const auto pa = map_uv(a.u, a.v);
        const auto pb = map_uv(b.u, b.v);
        const auto pc = map_uv(c.u, c.v);
        line(*image, pa, pb);
        line(*image, pb, pc);
        line(*image, pc, pa);
    }
}

RgbaImage make_canvas(int width, int height, std::uint8_t background = 0U) {
    // Dark (default), grey, light, black.
    static constexpr std::uint8_t kBackgrounds[4][3] = {{18U, 18U, 22U}, {72U, 74U, 80U}, {196U, 198U, 204U}, {0U, 0U, 0U}};
    const auto& bg = kBackgrounds[background & 3U];
    RgbaImage image;
    image.width = std::clamp(width, 1, 2048);
    image.height = std::clamp(height, 1, 2048);
    image.pixels.assign(
        static_cast<std::size_t>(image.width * image.height * 4), 0U);
    for (std::size_t i = 0U; i < image.pixels.size(); i += 4U) {
        image.pixels[i + 0U] = bg[0];
        image.pixels[i + 1U] = bg[1];
        image.pixels[i + 2U] = bg[2];
        image.pixels[i + 3U] = 255U;
    }
    return image;
}

// Smooth per-vertex light for the current (posed) positions. Face normals are
// accumulated per vertex (area weighted, oriented by the source normals when
// the mesh has them) and shared between coincident vertices whose source
// normals agree, so strip seams are smooth and authored hard edges stay hard.
// Light: two-sided, from the camera, up-left.
[[nodiscard]] std::vector<float> vertex_light(const Mesh& mesh, float yaw, float pitch, float radius) {
    const std::size_t n = mesh.vertices.size();
    std::vector<Vec3> acc(n);
    const bool source = mesh.has_normal0();
    for (std::size_t t = 0U; t + 2U < mesh.indices.size(); t += 3U) {
        const auto ia = mesh.indices[t], ib = mesh.indices[t + 1U], ic = mesh.indices[t + 2U];
        if (ia >= n || ib >= n || ic >= n) continue;
        const auto& a = mesh.vertices[ia];
        const auto& b = mesh.vertices[ib];
        const auto& c = mesh.vertices[ic];
        const Vec3 e1{b.x - a.x, b.y - a.y, b.z - a.z};
        const Vec3 e2{c.x - a.x, c.y - a.y, c.z - a.z};
        Vec3 f{e1.y * e2.z - e1.z * e2.y, e1.z * e2.x - e1.x * e2.z, e1.x * e2.y - e1.y * e2.x};
        if (source) {
            const auto& na = mesh.normal0[ia];
            const auto& nb = mesh.normal0[ib];
            const auto& nc = mesh.normal0[ic];
            const float d = f.x * (na.x + nb.x + nc.x) + f.y * (na.y + nb.y + nc.y) + f.z * (na.z + nb.z + nc.z);
            if (d < 0.0F) f = {-f.x, -f.y, -f.z};
        }
        for (const auto v : {ia, ib, ic}) {
            acc[v].x += f.x;
            acc[v].y += f.y;
            acc[v].z += f.z;
        }
    }
    std::vector<std::uint32_t> group(n);
    for (std::size_t v = 0U; v < n; ++v) group[v] = static_cast<std::uint32_t>(v);
    if (source && radius > 0.0F) {
        const float pq = 1.0e4F / radius;
        std::unordered_map<std::uint64_t, std::uint32_t> first;
        first.reserve(n);
        const auto q = [](float x, float s) {
            return static_cast<std::uint64_t>(static_cast<std::int64_t>(std::lround(x * s)) & 0xFFFFF);
        };
        for (std::size_t v = 0U; v < n; ++v) {
            const auto& nm = mesh.normal0[v];
            if (nm.x == 0.0F && nm.y == 0.0F && nm.z == 0.0F) continue;
            const auto& p = mesh.vertices[v];
            std::uint64_t key = q(p.x, pq) | (q(p.y, pq) << 20U) | (q(p.z, pq) << 40U);
            key ^= (q(nm.x, 16.0F) * 0x9E3779B97F4A7C15ULL) ^ (q(nm.y, 16.0F) * 0xC2B2AE3D27D4EB4FULL) ^
                   (q(nm.z, 16.0F) * 0x165667B19E3779F9ULL);
            const auto [it, inserted] = first.try_emplace(key, static_cast<std::uint32_t>(v));
            if (!inserted) {
                const auto g = it->second;
                group[v] = g;
                acc[g].x += acc[v].x;
                acc[g].y += acc[v].y;
                acc[g].z += acc[v].z;
            }
        }
    }
    std::vector<float> out(n, 0.9F);
    constexpr float lx = -0.30F, ly = 0.45F, lz = -0.84F;
    for (std::size_t v = 0U; v < n; ++v) {
        const auto& a = acc[group[v]];
        const float len = std::sqrt(a.x * a.x + a.y * a.y + a.z * a.z);
        if (!(len > 1.0e-20F)) continue;
        const auto r = rotate({a.x / len, a.y / len, a.z / len}, yaw, pitch);
        out[v] = std::fabs(r.x * lx + r.y * ly + r.z * lz);
    }
    return out;
}

}  // namespace

namespace {

// ---- Room pass (stage_room.h) -------------------------------------------
//
// The game hands every SCM object to the GPU and only sorts them by view
// depth (0x1402F9680 quantises the view z into 126 buckets, 0x140300740).
// On the CPU the same idea pays off: the room is prepared once per frame
// (each vertex moved into camera space once, triangles clipped at a near
// plane, faces turned away from the camera dropped, projected) and sorted
// front to back, so the depth test rejects hidden pixels before any texture
// is sampled. Rasterisation then runs per row band on several cores.

struct RoomSv {
    float x, y, iz, uz, vz, rz, gz, bz, az;
};

struct RoomTri {
    RoomSv a, b, c;
    const ImagePreview* texture{};
    float light{1.0F};
    float nearest{};  // smallest camera z, for the sort
    bool translucent{};
    bool colored{};
};

struct RoomFrame {
    std::vector<RoomTri> opaque;       // front to back
    std::vector<RoomTri> translucent;  // back to front
};

// Integer bilinear sample (8-bit weights, wrapping); rgba out 0..255.
[[nodiscard]] inline bool sample_bilinear_fast(const ImagePreview& texture, float u, float v,
                                               int* rgba) noexcept {
    if (!std::isfinite(u) || !std::isfinite(v)) return false;
    const int w = static_cast<int>(texture.width);
    const int h = static_cast<int>(texture.height);
    const float fx = (u - std::floor(u)) * static_cast<float>(w) - 0.5F;
    const float fy = (v - std::floor(v)) * static_cast<float>(h) - 0.5F;
    const float flx = std::floor(fx), fly = std::floor(fy);
    const int wx = static_cast<int>((fx - flx) * 256.0F);
    const int wy = static_cast<int>((fy - fly) * 256.0F);
    int x0 = static_cast<int>(flx), y0 = static_cast<int>(fly);
    if (x0 < 0) x0 += w;
    if (y0 < 0) y0 += h;
    if (x0 >= w) x0 -= w;
    if (y0 >= h) y0 -= h;
    const int x1 = x0 + 1 == w ? 0 : x0 + 1;
    const int y1 = y0 + 1 == h ? 0 : y0 + 1;
    const std::uint8_t* base = texture.rgba8.data();
    const std::uint8_t* p00 = base + (static_cast<std::size_t>(y0) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x0)) * 4U;
    const std::uint8_t* p10 = base + (static_cast<std::size_t>(y0) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x1)) * 4U;
    const std::uint8_t* p01 = base + (static_cast<std::size_t>(y1) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x0)) * 4U;
    const std::uint8_t* p11 = base + (static_cast<std::size_t>(y1) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x1)) * 4U;
    for (int k = 0; k < 4; ++k) {
        const int top = p00[k] * 256 + (p10[k] - p00[k]) * wx;
        const int bottom = p01[k] * 256 + (p11[k] - p01[k]) * wx;
        rgba[k] = (top * 256 + (bottom - top) * wy) >> 16;
    }
    return true;
}

[[nodiscard]] RoomFrame prepare_room(const ViewState& view, const CameraFrame& frame, float zoom, int width,
                                     int height) {
    RoomFrame out;
    const Mesh& rm = *view.room_mesh;
    const bool uv = rm.has_uv0();
    const bool colored = rm.has_color0();
    const bool normals = rm.has_normal0();
    const bool textured = uv && view.room_textures != nullptr && view.room_texture_slots != nullptr &&
        view.room_texture_slots->size() == rm.indices.size() / 3U;
    const auto* soft = view.room_translucent_triangles != nullptr &&
            view.room_translucent_triangles->size() == rm.indices.size() / 3U
        ? view.room_translucent_triangles
        : nullptr;
    const float near_z = std::max(1.0F, frame.radius * 0.05F);
    const float focal = zoom * frame.focal_px;
    const float hw = static_cast<float>(width) * 0.5F;
    const float hh = static_cast<float>(height) * 0.5F;
    const float cd = frame.camera_distance;

    struct Cv {
        float x, y, z, u, v, r, g, b, a;
    };
    // Every room vertex into camera space once.
    std::vector<Cv> cam(rm.vertices.size());
    for (std::size_t i = 0U; i < rm.vertices.size(); ++i) {
        const auto w = room_place(view, rm.vertices[i]);
        const auto r = rotate({w.x - frame.center.x, w.y - frame.center.y, w.z - frame.center.z},
                              view.yaw_radians, view.pitch_radians);
        Cv c{r.x - frame.pan_x, r.y - frame.pan_y, r.z + cd, 0.0F, 0.0F, 128.0F, 128.0F, 128.0F, 128.0F};
        if (uv) {
            c.u = rm.uv0[i].u;
            c.v = rm.uv0[i].v;
        }
        if (colored) {
            c.r = rm.color0[i][0];
            c.g = rm.color0[i][1];
            c.b = rm.color0[i][2];
            c.a = rm.color0[i][3];
        }
        cam[i] = c;
    }
    const auto mix = [](const Cv& a, const Cv& b, float t) {
        const auto l = [t](float p, float q) { return p + (q - p) * t; };
        return Cv{l(a.x, b.x), l(a.y, b.y), l(a.z, b.z), l(a.u, b.u), l(a.v, b.v),
                  l(a.r, b.r), l(a.g, b.g), l(a.b, b.b), l(a.a, b.a)};
    };

    out.opaque.reserve(rm.indices.size() / 3U);
    Cv clipped[4];
    for (std::size_t t = 0U; t + 2U < rm.indices.size(); t += 3U) {
        const std::uint32_t idx[3] = {rm.indices[t], rm.indices[t + 1U], rm.indices[t + 2U]};
        if (idx[0] >= cam.size() || idx[1] >= cam.size() || idx[2] >= cam.size()) continue;
        const Cv poly[3] = {cam[idx[0]], cam[idx[1]], cam[idx[2]]};
        if (poly[0].z < near_z && poly[1].z < near_z && poly[2].z < near_z) continue;
        // Whole triangle beside the view: skip before any more work.
        const float lim = 1.2F;
        const auto outside = [&](auto&& test) { return test(poly[0]) && test(poly[1]) && test(poly[2]); };
        const bool in_front = poly[0].z >= near_z && poly[1].z >= near_z && poly[2].z >= near_z;
        if (in_front && (outside([&](const Cv& c) { return c.x * focal > (hw * lim) * c.z; }) ||
            outside([&](const Cv& c) { return -c.x * focal > (hw * lim) * c.z; }) ||
            outside([&](const Cv& c) { return c.y * focal > (hh * lim) * c.z; }) ||
            outside([&](const Cv& c) { return -c.y * focal > (hh * lim) * c.z; }))) {
            continue;
        }

        float facing = 1.0F;
        if (normals) {
            const Vec3 n{rm.normal0[idx[0]].x + rm.normal0[idx[1]].x + rm.normal0[idx[2]].x,
                         rm.normal0[idx[0]].y + rm.normal0[idx[1]].y + rm.normal0[idx[2]].y,
                         rm.normal0[idx[0]].z + rm.normal0[idx[1]].z + rm.normal0[idx[2]].z};
            const auto nr = rotate(room_turn(view, n), view.yaw_radians, view.pitch_radians);
            const float nl = std::sqrt(nr.x * nr.x + nr.y * nr.y + nr.z * nr.z);
            const float pl = std::sqrt(poly[0].x * poly[0].x + poly[0].y * poly[0].y + poly[0].z * poly[0].z);
            if (nl > 0.0F && pl > 0.0F) {
                const float d = (nr.x * poly[0].x + nr.y * poly[0].y + nr.z * poly[0].z) / (nl * pl);
                if (d > 0.0F) continue;  // turned away from the camera
                facing = -d;
            }
        }

        int count = 0;
        for (int i = 0; i < 3; ++i) {
            const auto& a = poly[i];
            const auto& b = poly[(i + 1) % 3];
            const bool ina = a.z >= near_z;
            const bool inb = b.z >= near_z;
            if (ina) clipped[count++] = a;
            if (ina != inb) clipped[count++] = mix(a, b, (near_z - a.z) / (b.z - a.z));
        }
        if (count < 3) continue;

        const ImagePreview* texture = nullptr;
        if (textured) {
            const auto slot = (*view.room_texture_slots)[t / 3U];
            if (slot != kNoTextureSlot && slot < view.room_textures->size() &&
                (*view.room_textures)[slot].available()) {
                texture = &(*view.room_textures)[slot];
            }
        }
        const bool neutral = texture == nullptr && view.fallback_texture != nullptr &&
            view.fallback_texture->available();
        if (neutral) texture = view.fallback_texture;
        // Stages are prelit (COLOR0); a soft head light keeps unlit ones readable.
        const float light = colored && !neutral ? 1.0F : (neutral ? 0.45F + 0.55F * facing : 0.7F + 0.3F * facing);
        const bool translucent = soft != nullptr && (*soft)[t / 3U] != 0U && !neutral;

        RoomSv s[4];
        float nearest = std::numeric_limits<float>::max();
        for (int i = 0; i < count; ++i) {
            const auto& c = clipped[i];
            const float iz = 1.0F / c.z;
            s[i] = {hw + focal * c.x * iz, hh - focal * c.y * iz, iz, c.u * iz, c.v * iz,
                    c.r * iz, c.g * iz, c.b * iz, c.a * iz};
            nearest = std::min(nearest, c.z);
        }
        for (int k = 1; k + 1 < count; ++k) {
            RoomTri tri{s[0], s[k], s[k + 1], texture, light, nearest, translucent, colored && !neutral};
            (translucent ? out.translucent : out.opaque).push_back(tri);
        }
    }
    std::sort(out.opaque.begin(), out.opaque.end(),
              [](const RoomTri& a, const RoomTri& b) { return a.nearest < b.nearest; });
    std::sort(out.translucent.begin(), out.translucent.end(),
              [](const RoomTri& a, const RoomTri& b) { return a.nearest > b.nearest; });
    return out;
}

// One room triangle inside rows [row_begin, row_end). Opaque triangles write
// depth; translucent ones blend their soft texels without writing it.
void raster_room(const RoomTri& tri, bool translucent_pass, bool smooth, float cd, int row_begin, int row_end,
                 RgbaImage& image, std::vector<float>& depth) {
    const RoomSv& a = tri.a;
    const RoomSv& b = tri.b;
    const RoomSv& c = tri.c;
    const P2 pa{a.x, a.y, 0.0F}, pb{b.x, b.y, 0.0F}, pc{c.x, c.y, 0.0F};
    const float area = edge(pa, pb, c.x, c.y);
    if (std::fabs(area) < 1.0e-6F) return;
    const int y0 = std::max(row_begin, static_cast<int>(std::floor(std::min({a.y, b.y, c.y}))));
    const int y1 = std::min(row_end - 1, static_cast<int>(std::ceil(std::max({a.y, b.y, c.y}))));
    if (y0 > y1) return;
    const int x0 = std::max(0, static_cast<int>(std::floor(std::min({a.x, b.x, c.x}))));
    const int x1 = std::min(image.width - 1, static_cast<int>(std::ceil(std::max({a.x, b.x, c.x}))));
    if (x0 > x1) return;
    const float inv_area = 1.0F / area;
    const float light = tri.light;
    for (int y = y0; y <= y1; ++y) {
        const float py = static_cast<float>(y) + 0.5F;
        for (int x = x0; x <= x1; ++x) {
            const float px = static_cast<float>(x) + 0.5F;
            const float w0 = edge(pb, pc, px, py) * inv_area;
            const float w1 = edge(pc, pa, px, py) * inv_area;
            const float w2 = edge(pa, pb, px, py) * inv_area;
            if (w0 < 0.0F || w1 < 0.0F || w2 < 0.0F) continue;
            const float iz = w0 * a.iz + w1 * b.iz + w2 * c.iz;
            if (!(iz > 0.0F)) continue;
            const float zc = 1.0F / iz;
            const float dz = zc - cd;
            const auto pi = static_cast<std::size_t>(y * image.width + x);
            if (dz >= depth[pi]) continue;
            const auto at = [&](float p, float q, float r) { return (w0 * p + w1 * q + w2 * r) * zc; };
            int texel[4] = {128, 128, 128, 255};
            if (tri.texture != nullptr) {
                const float u = at(a.uz, b.uz, c.uz);
                const float v = at(a.vz, b.vz, c.vz);
                if (smooth) {
                    if (!sample_bilinear_fast(*tri.texture, u, v, texel)) continue;
                } else {
                    std::uint8_t tr = 0U, tg = 0U, tb = 0U, ta = 0U;
                    if (!sample_texture(*tri.texture, u, v, &tr, &tg, &tb, &ta)) continue;
                    texel[0] = tr;
                    texel[1] = tg;
                    texel[2] = tb;
                    texel[3] = ta;
                }
            }
            if (texel[3] < 8) continue;
            const bool opaque = texel[3] >= 240 || !tri.translucent;
            if (opaque == translucent_pass) continue;
            if (!tri.translucent && texel[3] < 32) continue;  // alpha-tested cut-outs
            float cr = static_cast<float>(texel[0]), cg = static_cast<float>(texel[1]), cb = static_cast<float>(texel[2]);
            if (tri.colored) {
                // PS2 modulate: texel x vertex colour / 0x80.
                cr *= at(a.rz, b.rz, c.rz) * (1.0F / 128.0F);
                cg *= at(a.gz, b.gz, c.gz) * (1.0F / 128.0F);
                cb *= at(a.bz, b.bz, c.bz) * (1.0F / 128.0F);
            }
            const auto o = pi * 4U;
            if (!opaque) {
                const float k = static_cast<float>(texel[3]) * (1.0F / 255.0F);
                const auto mixc = [&](std::size_t ch, float value) {
                    const float d = image.pixels[o + ch];
                    image.pixels[o + ch] = static_cast<std::uint8_t>(
                        std::clamp(static_cast<int>(d + (value * light - d) * k), 0, 255));
                };
                mixc(0U, cr);
                mixc(1U, cg);
                mixc(2U, cb);
                continue;
            }
            depth[pi] = dz;
            image.pixels[o + 0U] = static_cast<std::uint8_t>(std::clamp(static_cast<int>(cr * light), 0, 255));
            image.pixels[o + 1U] = static_cast<std::uint8_t>(std::clamp(static_cast<int>(cg * light), 0, 255));
            image.pixels[o + 2U] = static_cast<std::uint8_t>(std::clamp(static_cast<int>(cb * light), 0, 255));
            image.pixels[o + 3U] = 255U;
        }
    }
}

// Runs band(row_begin, row_end) over the image rows on several cores (row
// chunks handed out in order; every pixel belongs to exactly one band, so
// the result is the same as a single-threaded pass).
template <class Band>
void for_row_bands(int height, Band&& band) {
    constexpr int kRows = 32;
    const int chunks = (height + kRows - 1) / kRows;
    const unsigned hardware = std::thread::hardware_concurrency();
    const int threads = std::min(chunks, std::clamp(hardware == 0U ? 2 : static_cast<int>(hardware), 1, 8));
    if (threads <= 1) {
        band(0, height);
        return;
    }
    std::atomic<int> next{0};
    const auto work = [&] {
        for (int chunk = next.fetch_add(1); chunk < chunks; chunk = next.fetch_add(1)) {
            band(chunk * kRows, std::min(height, (chunk + 1) * kRows));
        }
    };
    std::vector<std::thread> pool;
    pool.reserve(static_cast<std::size_t>(threads - 1));
    try {
        for (int i = 1; i < threads; ++i) pool.emplace_back(work);
    } catch (...) {
        // No more threads: the ones started and this one finish the work.
    }
    work();
    for (auto& thread : pool) thread.join();
}

}  // namespace

RgbaImage render_uv_map(std::span<const Vec2> coordinates,
    std::span<const std::uint32_t> indices, int width, int height, float zoom) {
    auto image = make_canvas(width, height);
    ViewState view;
    view.zoom = zoom;
    draw_uv_layout(coordinates, indices, view, &image);
    return image;
}

RgbaImage render_view(const Mesh& mesh, int width, int height,
                      const ViewState& view,
                      const HierarchyOverlay* hierarchy,
                      const std::vector<std::uint32_t>* triangle_texture_slots,
                      const std::vector<ImagePreview>* textures) {
    auto image = make_canvas(width, height, view.background);
    if (mesh.vertices.empty() || mesh.indices.size() < 3U) return image;

    if (view.uv_layout) {
        if (mesh.has_uv0()) draw_uv_layout(mesh.uv0, mesh.indices, view, &image);
        return image;
    }

    // Previously this projected with a constant screen-space scale regardless
    // of depth (r.x * scale, no divide by z) -- a parallel/orthographic
    // projection, not perspective at all, which reads as flattened or
    // inverted-depth ("reverse perspective") compared to a normal camera.
    // compute_camera_frame's camera_distance pushes a real camera back from
    // the model center; project_in_frame's divide by z_cam is the actual
    // perspective divide that was missing.
    const auto frame = view_frame(mesh, view, image.width, image.height);
    const float zoom = std::clamp(view.zoom, 0.15F, 8.0F);
    const float radius = frame.radius;

    const auto project = [&](const Vec3& world) -> P2 {
        return project_in_frame(frame, world, view.yaw_radians, view.pitch_radians, zoom,
                                image.width, image.height);
    };

    std::vector<P2> p;
    p.reserve(mesh.vertices.size());
    for (const auto& v : mesh.vertices) {
        p.push_back(project(v));
    }

    const bool textured = textures != nullptr && triangle_texture_slots != nullptr &&
        mesh.has_uv0() && mesh.indices.size() % 3U == 0U &&
        triangle_texture_slots->size() == mesh.indices.size() / 3U;

    std::vector<float> depth(
        static_cast<std::size_t>(image.width * image.height),
        std::numeric_limits<float>::infinity());

    // Fills the rows [row_begin, row_end) of one projected triangle; `pixel`
    // gets (x, y, depth index, z).
    const auto fill = [&image](const P2& a, const P2& b, const P2& c, int row_begin, int row_end, auto&& pixel) {
        const float area = edge(a, b, c.x, c.y);
        if (std::fabs(area) < 1.0e-6F) return;
        const int x0 = std::max(0, static_cast<int>(std::floor(std::min({a.x, b.x, c.x}))));
        const int y0 = std::max(row_begin, static_cast<int>(std::floor(std::min({a.y, b.y, c.y}))));
        const int x1 = std::min(image.width - 1,
                                static_cast<int>(std::ceil(std::max({a.x, b.x, c.x}))));
        const int y1 = std::min(row_end - 1,
                                static_cast<int>(std::ceil(std::max({a.y, b.y, c.y}))));
        for (int y = y0; y <= y1; ++y) {
            for (int x = x0; x <= x1; ++x) {
                const float px = static_cast<float>(x) + 0.5F;
                const float py = static_cast<float>(y) + 0.5F;
                const float w0 = edge(b, c, px, py) / area;
                const float w1 = edge(c, a, px, py) / area;
                const float w2 = edge(a, b, px, py) / area;
                if (w0 < 0.0F || w1 < 0.0F || w2 < 0.0F) continue;
                pixel(x, y, static_cast<std::size_t>(y * image.width + x),
                      w0 * a.z + w1 * b.z + w2 * c.z);
            }
        }
    };

    // Everything per frame is prepared once; the row bands only rasterise.
    const bool room = view.room_mesh != nullptr && !view.wireframe &&
        view.room_mesh->indices.size() >= 3U;
    RoomFrame room_frame;
    if (room) room_frame = prepare_room(view, frame, zoom, image.width, image.height);
    const bool smooth_room = !view.fast_preview;
    const float cd = frame.camera_distance;

    const bool floor = view.floor && !view.wireframe;
    P2 floor_quad[4]{};
    if (floor && !room) {
        const float half = radius * 1.4F;
        const Vec3 corners[4] = {
            {frame.center.x - half, view.floor_y, frame.center.z - half},
            {frame.center.x + half, view.floor_y, frame.center.z - half},
            {frame.center.x + half, view.floor_y, frame.center.z + half},
            {frame.center.x - half, view.floor_y, frame.center.z + half},
        };
        for (int k = 0; k < 4; ++k) floor_quad[k] = project(corners[k]);
    }
    const auto floor_pixel = [&](int x, int y, std::size_t pi, float z) {
        if (z >= depth[pi]) return;
        depth[pi] = z;
        put_rgba(image, x, y, 64U, 67U, 76U, 255U);
    };

    std::vector<P2> shadow;
    std::vector<std::uint8_t> shadowed;
    if (floor && view.floor_shadow.size() >= 3U) {
        shadow.reserve(view.floor_shadow.size());
        for (const auto& point : view.floor_shadow) shadow.push_back(project(point));
        shadowed.assign(depth.size(), 0U);
    }
    const float shadow_tolerance = radius * 0.01F;
    const auto shadow_pixel = [&](int, int, std::size_t pi, float z) {
        // Darken each floor pixel once where the footprint lands and the floor
        // is what the camera sees there (the model in front keeps its colour).
        if (shadowed[pi] != 0U || z > depth[pi] + shadow_tolerance) return;
        shadowed[pi] = 1U;
        const auto o = pi * 4U;
        for (std::size_t k = 0U; k < 3U; ++k) {
            image.pixels[o + k] = static_cast<std::uint8_t>(image.pixels[o + k] * 45U / 100U);
        }
    };

    const auto lights = view.wireframe || view.unlit ? std::vector<float>{}
                                       : vertex_light(mesh, view.yaw_radians, view.pitch_radians, radius);
    const bool colored = mesh.has_color0();
    const bool smooth_model = view.smooth_textures && !view.fast_preview;

    // The model's triangles inside rows [row_begin, row_end).
    const auto model_band = [&](int row_begin, int row_end) {
        for (std::size_t t = 0U; t + 2U < mesh.indices.size(); t += 3U) {
            const auto ia = mesh.indices[t + 0U];
            const auto ib = mesh.indices[t + 1U];
            const auto ic = mesh.indices[t + 2U];
            if (ia >= p.size() || ib >= p.size() || ic >= p.size()) continue;
            const P2 a = p[ia], b = p[ib], c = p[ic];
            const int y0 = std::max(row_begin, static_cast<int>(std::floor(std::min({a.y, b.y, c.y}))));
            const int y1 = std::min(row_end - 1, static_cast<int>(std::ceil(std::max({a.y, b.y, c.y}))));
            if (y0 > y1) continue;

            const std::uint8_t blend_mode = mesh.has_blend0() ? mesh.blend0[ia] : 0U;
            const ImagePreview* texture = nullptr;
            if (textured) {
                const auto slot = (*triangle_texture_slots)[t / 3U];
                if (slot != kNoTextureSlot && slot < textures->size() &&
                    (*textures)[slot].available()) {
                    texture = &(*textures)[slot];
                }
            }
            // No texture of its own: the neutral texture (neutral_texture.h).
            bool neutral = false;
            if (texture == nullptr && view.fallback_texture != nullptr && view.fallback_texture->available()) {
                texture = view.fallback_texture;
                neutral = true;
            }
            // Gouraud light: full range on the neutral texture, milder on real
            // textures (their shading is painted in); none on prelit COLOR0 or
            // additive / subtractive effects.
            const bool lit = !lights.empty() && (neutral || (!colored && blend_mode != 2U && blend_mode != 3U));
            const float lbase = neutral ? 0.45F : 0.72F;
            const float lgain = neutral ? 0.85F : 0.42F;

            const float area = edge(a, b, c.x, c.y);
            if (std::fabs(area) < 1.0e-6F) continue;
            const int x0 = std::max(0, static_cast<int>(std::floor(std::min({a.x, b.x, c.x}))));
            const int x1 = std::min(image.width - 1, static_cast<int>(std::ceil(std::max({a.x, b.x, c.x}))));

            for (int y = y0; y <= y1; ++y) {
                for (int x = x0; x <= x1; ++x) {
                    const float px = static_cast<float>(x) + 0.5F;
                    const float py = static_cast<float>(y) + 0.5F;
                    const float w0 = edge(b, c, px, py) / area;
                    const float w1 = edge(c, a, px, py) / area;
                    const float w2 = edge(a, b, px, py) / area;
                    if (w0 < 0.0F || w1 < 0.0F || w2 < 0.0F) continue;
                    const float z = w0 * a.z + w1 * b.z + w2 * c.z;
                    const auto pi = static_cast<std::size_t>(y * image.width + x);
                    if (z >= depth[pi]) continue;

                    if (texture != nullptr) {
                        float u = 0.0F, v = 0.0F;
                        if (mesh.has_uv0()) {
                            const auto& uva = mesh.uv0[ia];
                            const auto& uvb = mesh.uv0[ib];
                            const auto& uvc = mesh.uv0[ic];
                            u = w0 * uva.u + w1 * uvb.u + w2 * uvc.u;
                            v = w0 * uva.v + w1 * uvb.v + w2 * uvc.v;
                        }
                        std::uint8_t tr = 0U, tg = 0U, tb = 0U, ta = 0U;
                        bool sampled = false;
                        if (smooth_model) {
                            int texel[4];
                            if (sample_bilinear_fast(*texture, u, v, texel)) {
                                tr = static_cast<std::uint8_t>(texel[0]);
                                tg = static_cast<std::uint8_t>(texel[1]);
                                tb = static_cast<std::uint8_t>(texel[2]);
                                ta = static_cast<std::uint8_t>(texel[3]);
                                sampled = true;
                            }
                        } else {
                            sampled = sample_texture(*texture, u, v, &tr, &tg, &tb, &ta);
                        }
                        if (sampled) {
                            if (colored) {
                                // PS2 modulate: texel x vertex colour / 0x80.
                                const auto& ca = mesh.color0[ia];
                                const auto& cb = mesh.color0[ib];
                                const auto& cc = mesh.color0[ic];
                                const auto mod = [&](std::uint8_t t8, std::size_t k) {
                                    const float vc = w0 * ca[k] + w1 * cb[k] + w2 * cc[k];
                                    return static_cast<std::uint8_t>(
                                        std::clamp(static_cast<int>(t8 * vc / 128.0F), 0, 255));
                                };
                                tr = mod(tr, 0U);
                                tg = mod(tg, 1U);
                                tb = mod(tb, 2U);
                                ta = mod(ta, 3U);
                            }
                            if (lit) {
                                const float light =
                                    lbase + lgain * (w0 * lights[ia] + w1 * lights[ib] + w2 * lights[ic]);
                                const auto shade = [light](std::uint8_t c8) {
                                    return static_cast<std::uint8_t>(
                                        std::clamp(static_cast<int>(static_cast<float>(c8) * light), 0, 255));
                                };
                                tr = shade(tr);
                                tg = shade(tg);
                                tb = shade(tb);
                            }
                            if (ta == 0U) continue;
                            if (blend_mode == 2U || blend_mode == 3U) {
                                // Additive / subtractive effects do not occlude.
                                blend_rgba(image, x, y, tr, tg, tb, ta, blend_mode);
                                continue;
                            }
                            depth[pi] = z;
                            put_rgba(image, x, y, tr, tg, tb, ta);
                            continue;
                        }
                    }

                    depth[pi] = z;
                    const float zn = 0.5F + 0.5F * std::tanh(-z / radius);
                    const auto shade = static_cast<std::uint8_t>(145.0F + 80.0F * zn);
                    put_pixel(image, x, y, shade);
                }
            }
        }
    };

    if (view.wireframe) {
        for (std::size_t t = 0U; t + 2U < mesh.indices.size(); t += 3U) {
            const auto ia = mesh.indices[t + 0U];
            const auto ib = mesh.indices[t + 1U];
            const auto ic = mesh.indices[t + 2U];
            if (ia >= p.size() || ib >= p.size() || ic >= p.size()) continue;
            line(image, p[ia], p[ib]);
            line(image, p[ib], p[ic]);
            line(image, p[ic], p[ia]);
        }
    } else {
        // Per band: opaque room (front to back), floor, model, soft room
        // texels (back to front), then the shadow footprint.
        for_row_bands(image.height, [&](int row_begin, int row_end) {
            for (const auto& tri : room_frame.opaque) {
                raster_room(tri, false, smooth_room, cd, row_begin, row_end, image, depth);
            }
            if (floor && !room) {
                fill(floor_quad[0], floor_quad[1], floor_quad[2], row_begin, row_end, floor_pixel);
                fill(floor_quad[0], floor_quad[2], floor_quad[3], row_begin, row_end, floor_pixel);
            }
            model_band(row_begin, row_end);
            for (const auto& tri : room_frame.translucent) {
                raster_room(tri, true, smooth_room, cd, row_begin, row_end, image, depth);
            }
            for (std::size_t t = 0U; t + 2U < shadow.size(); t += 3U) {
                fill(shadow[t], shadow[t + 1U], shadow[t + 2U], row_begin, row_end, shadow_pixel);
            }
        });
    }

    for (std::size_t i = 0U; i + 1U < view.overlay_lines.size(); i += 2U) {
        line_rgba(image, project(view.overlay_lines[i]), project(view.overlay_lines[i + 1U]), 255U, 90U, 60U);
    }

    if (hierarchy != nullptr && hierarchy->available()) {
        std::vector<P2> hp;
        hp.reserve(hierarchy->points.size());
        for (const auto& point : hierarchy->points) {
            hp.push_back(project(point));
        }
        for (const auto& edge_value : hierarchy->edges) {
            if (edge_value.parent >= hp.size() || edge_value.child >= hp.size()) continue;
            line(image, hp[edge_value.parent], hp[edge_value.child], 255U);
        }
        for (const auto& point : hp) marker(image, point, 255U);
    }

    return image;
}

ViewPick pick_view(const Mesh& mesh, int width, int height, const ViewState& view, float px, float py,
                   const HierarchyOverlay* hierarchy, float max_joint_px) {
    ViewPick out;
    if (mesh.vertices.empty()) return out;
    const int w = std::clamp(width, 1, 2048);
    const int h = std::clamp(height, 1, 2048);
    const auto frame = view_frame(mesh, view, w, h);
    const float zoom = std::clamp(view.zoom, 0.15F, 8.0F);
    const float focal = zoom * frame.focal_px;
    // Camera-space ray from the eye through the pixel centre.
    const Vec3 dir{(px + 0.5F - static_cast<float>(w) * 0.5F) / focal,
                   -(py + 0.5F - static_cast<float>(h) * 0.5F) / focal, 1.0F};
    const auto to_camera = [&](const Vec3& world) {
        const auto r = rotate({world.x - frame.center.x, world.y - frame.center.y, world.z - frame.center.z},
                              view.yaw_radians, view.pitch_radians);
        return Vec3{r.x - frame.pan_x, r.y - frame.pan_y, r.z + frame.camera_distance};
    };
    // Moller-Trumbore; t along dir (camera z), barycentrics u, v.
    const auto hit = [&](const Vec3& a, const Vec3& b, const Vec3& c, float* t, float* u, float* v) {
        const Vec3 e1{b.x - a.x, b.y - a.y, b.z - a.z};
        const Vec3 e2{c.x - a.x, c.y - a.y, c.z - a.z};
        const Vec3 p{dir.y * e2.z - dir.z * e2.y, dir.z * e2.x - dir.x * e2.z, dir.x * e2.y - dir.y * e2.x};
        const float det = e1.x * p.x + e1.y * p.y + e1.z * p.z;
        if (std::fabs(det) < 1.0e-9F) return false;
        const float inv = 1.0F / det;
        const Vec3 s{-a.x, -a.y, -a.z};
        *u = (s.x * p.x + s.y * p.y + s.z * p.z) * inv;
        if (*u < 0.0F || *u > 1.0F) return false;
        const Vec3 q{s.y * e1.z - s.z * e1.y, s.z * e1.x - s.x * e1.z, s.x * e1.y - s.y * e1.x};
        *v = (dir.x * q.x + dir.y * q.y + dir.z * q.z) * inv;
        if (*v < 0.0F || *u + *v > 1.0F) return false;
        *t = (e2.x * q.x + e2.y * q.y + e2.z * q.z) * inv;
        return *t > 1.0e-3F;
    };
    float best = std::numeric_limits<float>::infinity();
    for (std::size_t t = 0U; t + 2U < mesh.indices.size(); t += 3U) {
        const auto ia = mesh.indices[t], ib = mesh.indices[t + 1U], ic = mesh.indices[t + 2U];
        if (ia >= mesh.vertices.size() || ib >= mesh.vertices.size() || ic >= mesh.vertices.size()) continue;
        float d = 0.0F, u = 0.0F, v = 0.0F;
        if (hit(to_camera(mesh.vertices[ia]), to_camera(mesh.vertices[ib]), to_camera(mesh.vertices[ic]), &d, &u, &v) &&
            d < best) {
            best = d;
            out.model = true;
        }
    }
    if (view.room_mesh != nullptr) {
        const Mesh& rm = *view.room_mesh;
        const bool normals = rm.has_normal0();
        const float near_z = std::max(1.0F, frame.radius * 0.05F);
        for (std::size_t t = 0U; t + 2U < rm.indices.size(); t += 3U) {
            const auto ia = rm.indices[t], ib = rm.indices[t + 1U], ic = rm.indices[t + 2U];
            if (ia >= rm.vertices.size() || ib >= rm.vertices.size() || ic >= rm.vertices.size()) continue;
            const auto a = to_camera(room_place(view, rm.vertices[ia]));
            const auto b = to_camera(room_place(view, rm.vertices[ib]));
            const auto c = to_camera(room_place(view, rm.vertices[ic]));
            if (normals) {
                // Faces the room pass skips (turned away) are not hit either.
                const Vec3 n{rm.normal0[ia].x + rm.normal0[ib].x + rm.normal0[ic].x,
                             rm.normal0[ia].y + rm.normal0[ib].y + rm.normal0[ic].y,
                             rm.normal0[ia].z + rm.normal0[ib].z + rm.normal0[ic].z};
                const auto nr = rotate(room_turn(view, n), view.yaw_radians, view.pitch_radians);
                if (nr.x * a.x + nr.y * a.y + nr.z * a.z > 0.0F) continue;
            }
            float d = 0.0F, u = 0.0F, v = 0.0F;
            if (!hit(a, b, c, &d, &u, &v) || d >= best || d < near_z) continue;
            best = d;
            out.model = false;
            out.room = true;
            const auto& ra = rm.vertices[ia];
            const auto& rb = rm.vertices[ib];
            const auto& rc = rm.vertices[ic];
            const float k = 1.0F - u - v;
            out.room_point = {k * ra.x + u * rb.x + v * rc.x, k * ra.y + u * rb.y + v * rc.y,
                              k * ra.z + u * rb.z + v * rc.z};
            // Upward surface: source normals when present, else the plane.
            Vec3 up{};
            if (normals) {
                up = {rm.normal0[ia].x + rm.normal0[ib].x + rm.normal0[ic].x,
                      rm.normal0[ia].y + rm.normal0[ib].y + rm.normal0[ic].y,
                      rm.normal0[ia].z + rm.normal0[ib].z + rm.normal0[ic].z};
            } else {
                const Vec3 e1{rb.x - ra.x, rb.y - ra.y, rb.z - ra.z};
                const Vec3 e2{rc.x - ra.x, rc.y - ra.y, rc.z - ra.z};
                up = {e1.y * e2.z - e1.z * e2.y, e1.z * e2.x - e1.x * e2.z, e1.x * e2.y - e1.y * e2.x};
                up.y = std::fabs(up.y);
            }
            const float ul = std::sqrt(up.x * up.x + up.y * up.y + up.z * up.z);
            out.room_floor = ul > 0.0F && up.y / ul > 0.7F;
        }
    }
    if (hierarchy != nullptr && hierarchy->available()) {
        float nearest = max_joint_px;
        for (std::size_t i = 0U; i < hierarchy->points.size(); ++i) {
            const auto p = project_in_frame(frame, hierarchy->points[i], view.yaw_radians, view.pitch_radians,
                                            zoom, w, h);
            const float d = std::hypot(p.x - px, p.y - py);
            if (d <= nearest) {
                nearest = d;
                out.joint = static_cast<int>(i);
                out.joint_px = d;
            }
        }
    }
    return out;
}

std::vector<HierarchyScreenPoint> project_hierarchy_points(const Mesh& mesh,
    const HierarchyOverlay& hierarchy, int width, int height, const ViewState& view) {
    std::vector<HierarchyScreenPoint> out;
    if (mesh.vertices.empty() || !hierarchy.available()) return out;

    const int clamped_width = std::clamp(width, 1, 2048);
    const int clamped_height = std::clamp(height, 1, 2048);
    const auto frame = view_frame(mesh, view, clamped_width, clamped_height);
    const float zoom = std::clamp(view.zoom, 0.15F, 8.0F);

    out.reserve(hierarchy.points.size());
    for (const auto& point : hierarchy.points) {
        const auto p = project_in_frame(frame, point, view.yaw_radians, view.pitch_radians, zoom,
                                        clamped_width, clamped_height);
        out.push_back({p.x, p.y});
    }
    return out;
}

}  // namespace dmcresource
