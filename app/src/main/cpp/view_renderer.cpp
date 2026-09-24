#include "dmcresource/view_renderer.h"

#include <algorithm>
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
    return {static_cast<float>(width) * 0.5F + zoom * frame.focal_px * r.x * inv_z,
           static_cast<float>(height) * 0.5F - zoom * frame.focal_px * r.y * inv_z, r.z};
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

// Bilinear, wrapping (room textures are magnified a lot near the camera).
[[nodiscard]] bool sample_bilinear(const ImagePreview& texture, float u, float v, float* rgba) noexcept {
    if (!texture.available() || !std::isfinite(u) || !std::isfinite(v)) return false;
    const auto w = static_cast<int>(texture.width);
    const auto h = static_cast<int>(texture.height);
    if (texture.rgba8.size() < static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4U) return false;
    const float x = repeat_unit(u) * static_cast<float>(w) - 0.5F;
    const float y = repeat_unit(v) * static_cast<float>(h) - 0.5F;
    const float fx0 = std::floor(x), fy0 = std::floor(y);
    const float tx = x - fx0, ty = y - fy0;
    const int x0 = (static_cast<int>(fx0) % w + w) % w, x1 = (x0 + 1) % w;
    const int y0 = (static_cast<int>(fy0) % h + h) % h, y1 = (y0 + 1) % h;
    const auto at = [&](int px, int py, int k) {
        return static_cast<float>(texture.rgba8[(static_cast<std::size_t>(py) * static_cast<std::size_t>(w) +
                                                 static_cast<std::size_t>(px)) * 4U + static_cast<std::size_t>(k)]);
    };
    for (int k = 0; k < 4; ++k) {
        const float top = at(x0, y0, k) + (at(x1, y0, k) - at(x0, y0, k)) * tx;
        const float bottom = at(x0, y1, k) + (at(x1, y1, k) - at(x0, y1, k)) * tx;
        rgba[k] = top + (bottom - top) * ty;
    }
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

// Room pass (stage_room.h): the room triangles in camera space, clipped at
// a near plane (the room surrounds the camera), faces whose source normal
// points away from the camera skipped, textures interpolated perspective-
// correctly. Depth uses the model pass's convention (camera z - distance).
static void draw_room(const ViewState& view, const CameraFrame& frame, float zoom, RgbaImage& image,
               std::vector<float>& depth) {
    const Mesh& rm = *view.room_mesh;
    const bool uv = rm.has_uv0();
    const bool colored = rm.has_color0();
    const bool normals = rm.has_normal0();
    const bool textured = uv && view.room_textures != nullptr && view.room_texture_slots != nullptr &&
        view.room_texture_slots->size() == rm.indices.size() / 3U;
    const float near_z = std::max(1.0F, frame.radius * 0.05F);
    const float focal = zoom * frame.focal_px;
    const float hw = static_cast<float>(image.width) * 0.5F;
    const float hh = static_cast<float>(image.height) * 0.5F;
    const float cd = frame.camera_distance;

    struct Cv final { float x, y, z, u, v, r, g, b, a; };
    struct Sv final { float x, y, iz, uz, vz, rz, gz, bz, az; };
    const auto camera = [&](std::uint32_t i) {
        const auto& w = rm.vertices[i];
        const Vec3 local{w.x + view.room_offset.x - frame.center.x, w.y + view.room_offset.y - frame.center.y,
                         w.z + view.room_offset.z - frame.center.z};
        const auto r = rotate(local, view.yaw_radians, view.pitch_radians);
        Cv out{r.x, r.y, r.z + cd, 0.0F, 0.0F, 128.0F, 128.0F, 128.0F, 128.0F};
        if (uv) {
            out.u = rm.uv0[i].u;
            out.v = rm.uv0[i].v;
        }
        if (colored) {
            out.r = rm.color0[i][0];
            out.g = rm.color0[i][1];
            out.b = rm.color0[i][2];
            out.a = rm.color0[i][3];
        }
        return out;
    };
    const auto mix = [](const Cv& a, const Cv& b, float t) {
        const auto l = [t](float p, float q) { return p + (q - p) * t; };
        return Cv{l(a.x, b.x), l(a.y, b.y), l(a.z, b.z), l(a.u, b.u), l(a.v, b.v),
                  l(a.r, b.r), l(a.g, b.g), l(a.b, b.b), l(a.a, b.a)};
    };

    std::vector<Cv> poly;
    std::vector<Cv> clipped;
    std::vector<Sv> screen;
    const auto* soft = view.room_translucent_triangles != nullptr &&
            view.room_translucent_triangles->size() == rm.indices.size() / 3U
        ? view.room_translucent_triangles
        : nullptr;
    // Pass 0: opaque texels (depth written); pass 1: soft-alpha texels of
    // flagged triangles blended over what is already drawn.
    for (int pass = 0; pass < 2; ++pass) {
    for (std::size_t t = 0U; t + 2U < rm.indices.size(); t += 3U) {
        const bool translucent = soft != nullptr && (*soft)[t / 3U] != 0U;
        if (pass == 1 && !translucent) continue;
        const std::uint32_t idx[3] = {rm.indices[t], rm.indices[t + 1U], rm.indices[t + 2U]};
        if (idx[0] >= rm.vertices.size() || idx[1] >= rm.vertices.size() || idx[2] >= rm.vertices.size()) continue;
        poly = {camera(idx[0]), camera(idx[1]), camera(idx[2])};
        if (poly[0].z < near_z && poly[1].z < near_z && poly[2].z < near_z) continue;

        // Facing: source normal against the view ray of the first corner.
        float facing = 1.0F;
        if (normals) {
            const Vec3 n{rm.normal0[idx[0]].x + rm.normal0[idx[1]].x + rm.normal0[idx[2]].x,
                         rm.normal0[idx[0]].y + rm.normal0[idx[1]].y + rm.normal0[idx[2]].y,
                         rm.normal0[idx[0]].z + rm.normal0[idx[1]].z + rm.normal0[idx[2]].z};
            const auto nr = rotate(n, view.yaw_radians, view.pitch_radians);
            const float nl = std::sqrt(nr.x * nr.x + nr.y * nr.y + nr.z * nr.z);
            const float pl = std::sqrt(poly[0].x * poly[0].x + poly[0].y * poly[0].y + poly[0].z * poly[0].z);
            if (nl > 0.0F && pl > 0.0F) {
                const float d = (nr.x * poly[0].x + nr.y * poly[0].y + nr.z * poly[0].z) / (nl * pl);
                if (d > 0.0F) continue;  // turned away from the camera
                facing = -d;
            }
        }

        clipped.clear();
        for (std::size_t i = 0U; i < poly.size(); ++i) {
            const auto& a = poly[i];
            const auto& b = poly[(i + 1U) % poly.size()];
            const bool ina = a.z >= near_z;
            const bool inb = b.z >= near_z;
            if (ina) clipped.push_back(a);
            if (ina != inb) clipped.push_back(mix(a, b, (near_z - a.z) / (b.z - a.z)));
        }
        if (clipped.size() < 3U) continue;

        screen.clear();
        float minx = std::numeric_limits<float>::max(), maxx = -minx;
        float miny = minx, maxy = -minx;
        for (const auto& c : clipped) {
            const float iz = 1.0F / c.z;
            const Sv s{hw + focal * c.x * iz, hh - focal * c.y * iz, iz, c.u * iz, c.v * iz,
                       c.r * iz, c.g * iz, c.b * iz, c.a * iz};
            minx = std::min(minx, s.x);
            maxx = std::max(maxx, s.x);
            miny = std::min(miny, s.y);
            maxy = std::max(maxy, s.y);
            screen.push_back(s);
        }
        if (maxx < 0.0F || maxy < 0.0F || minx >= static_cast<float>(image.width) ||
            miny >= static_cast<float>(image.height)) {
            continue;
        }

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

        for (std::size_t k = 1U; k + 1U < screen.size(); ++k) {
            const Sv& a = screen[0];
            const Sv& b = screen[k];
            const Sv& c = screen[k + 1U];
            const P2 pa{a.x, a.y, 0.0F}, pb{b.x, b.y, 0.0F}, pc{c.x, c.y, 0.0F};
            const float area = edge(pa, pb, c.x, c.y);
            if (std::fabs(area) < 1.0e-6F) continue;
            const int x0 = std::max(0, static_cast<int>(std::floor(std::min({a.x, b.x, c.x}))));
            const int y0 = std::max(0, static_cast<int>(std::floor(std::min({a.y, b.y, c.y}))));
            const int x1 = std::min(image.width - 1, static_cast<int>(std::ceil(std::max({a.x, b.x, c.x}))));
            const int y1 = std::min(image.height - 1, static_cast<int>(std::ceil(std::max({a.y, b.y, c.y}))));
            for (int y = y0; y <= y1; ++y) {
                for (int x = x0; x <= x1; ++x) {
                    const float px = static_cast<float>(x) + 0.5F;
                    const float py = static_cast<float>(y) + 0.5F;
                    const float w0 = edge(pb, pc, px, py) / area;
                    const float w1 = edge(pc, pa, px, py) / area;
                    const float w2 = edge(pa, pb, px, py) / area;
                    if (w0 < 0.0F || w1 < 0.0F || w2 < 0.0F) continue;
                    const float iz = w0 * a.iz + w1 * b.iz + w2 * c.iz;
                    if (!(iz > 0.0F)) continue;
                    const float zc = 1.0F / iz;
                    const float dz = zc - cd;
                    const auto pi = static_cast<std::size_t>(y * image.width + x);
                    if (dz >= depth[pi]) continue;
                    const auto at = [&](float p, float q, float r) { return (w0 * p + w1 * q + w2 * r) * zc; };
                    float cr = 128.0F, cg = 128.0F, cb = 128.0F;
                    if (texture != nullptr) {
                        float texel[4];
                        if (!sample_bilinear(*texture, at(a.uz, b.uz, c.uz), at(a.vz, b.vz, c.vz), texel)) {
                            continue;
                        }
                        if (texel[3] < 8.0F) continue;
                        const bool opaque = texel[3] >= 240.0F || !translucent;
                        if (opaque ? pass == 1 : pass == 0) continue;
                        if (!translucent && texel[3] < 32.0F) continue;  // alpha-tested cut-outs
                        cr = texel[0];
                        cg = texel[1];
                        cb = texel[2];
                        if (!opaque) {
                            float rz = 1.0F, gz = 1.0F, bz = 1.0F;
                            if (colored) {
                                rz = at(a.rz, b.rz, c.rz) / 128.0F;
                                gz = at(a.gz, b.gz, c.gz) / 128.0F;
                                bz = at(a.bz, b.bz, c.bz) / 128.0F;
                            }
                            const float k = texel[3] / 255.0F;
                            const auto o = pi * 4U;
                            const auto mixc = [&](std::size_t ch, float value) {
                                const float d = image.pixels[o + ch];
                                image.pixels[o + ch] = static_cast<std::uint8_t>(
                                    std::clamp(static_cast<int>(d + (value * light - d) * k), 0, 255));
                            };
                            mixc(0U, cr * rz);
                            mixc(1U, cg * gz);
                            mixc(2U, cb * bz);
                            continue;
                        }
                    } else if (pass == 1) {
                        continue;
                    }
                    if (colored) {
                        // PS2 modulate: texel x vertex colour / 0x80.
                        cr = cr * at(a.rz, b.rz, c.rz) / 128.0F;
                        cg = cg * at(a.gz, b.gz, c.gz) / 128.0F;
                        cb = cb * at(a.bz, b.bz, c.bz) / 128.0F;
                    }
                    const auto out = [light](float value) {
                        return static_cast<std::uint8_t>(std::clamp(static_cast<int>(value * light), 0, 255));
                    };
                    depth[pi] = dz;
                    put_rgba(image, x, y, out(cr), out(cg), out(cb), 255U);
                }
            }
        }
    }
    }
}

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
    const auto frame = compute_camera_frame(
        view.framing_vertices.empty() ? std::span<const Vec3>{mesh.vertices}
                                      : view.framing_vertices,
        image.width, image.height);
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

    // Fills one projected triangle; `pixel` gets (x, y, depth index, z).
    const auto fill = [&image](const P2& a, const P2& b, const P2& c, auto&& pixel) {
        const float area = edge(a, b, c.x, c.y);
        if (std::fabs(area) < 1.0e-6F) return;
        const int x0 = std::max(0, static_cast<int>(std::floor(std::min({a.x, b.x, c.x}))));
        const int y0 = std::max(0, static_cast<int>(std::floor(std::min({a.y, b.y, c.y}))));
        const int x1 = std::min(image.width - 1,
                                static_cast<int>(std::ceil(std::max({a.x, b.x, c.x}))));
        const int y1 = std::min(image.height - 1,
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

    const bool room = view.room_mesh != nullptr && !view.wireframe &&
        view.room_mesh->indices.size() >= 3U;
    if (room) draw_room(view, frame, zoom, image, depth);
    const bool floor = view.floor && !view.wireframe;
    if (floor && !room) {
        const float half = radius * 1.4F;
        const Vec3 corners[4] = {
            {frame.center.x - half, view.floor_y, frame.center.z - half},
            {frame.center.x + half, view.floor_y, frame.center.z - half},
            {frame.center.x + half, view.floor_y, frame.center.z + half},
            {frame.center.x - half, view.floor_y, frame.center.z + half},
        };
        const P2 q[4] = {project(corners[0]), project(corners[1]), project(corners[2]),
                         project(corners[3])};
        const auto floor_pixel = [&](int x, int y, std::size_t pi, float z) {
            if (z >= depth[pi]) return;
            depth[pi] = z;
            put_rgba(image, x, y, 64U, 67U, 76U, 255U);
        };
        fill(q[0], q[1], q[2], floor_pixel);
        fill(q[0], q[2], q[3], floor_pixel);
    }

    const auto lights = view.wireframe || view.unlit ? std::vector<float>{}
                                       : vertex_light(mesh, view.yaw_radians, view.pitch_radians, radius);
    for (std::size_t t = 0U; t + 2U < mesh.indices.size(); t += 3U) {
        const auto ia = mesh.indices[t + 0U];
        const auto ib = mesh.indices[t + 1U];
        const auto ic = mesh.indices[t + 2U];
        if (ia >= p.size() || ib >= p.size() || ic >= p.size()) continue;
        const P2 a = p[ia], b = p[ib], c = p[ic];
        if (view.wireframe) {
            line(image, a, b);
            line(image, b, c);
            line(image, c, a);
            continue;
        }

        const bool colored = mesh.has_color0();
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
        const int x0 = std::max(
            0, static_cast<int>(std::floor(std::min({a.x, b.x, c.x}))));
        const int y0 = std::max(
            0, static_cast<int>(std::floor(std::min({a.y, b.y, c.y}))));
        const int x1 = std::min(
            image.width - 1,
            static_cast<int>(std::ceil(std::max({a.x, b.x, c.x}))));
        const int y1 = std::min(
            image.height - 1,
            static_cast<int>(std::ceil(std::max({a.y, b.y, c.y}))));

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
                    if (view.smooth_textures) {
                        float texel[4];
                        if (sample_bilinear(*texture, u, v, texel)) {
                            tr = static_cast<std::uint8_t>(texel[0] + 0.5F);
                            tg = static_cast<std::uint8_t>(texel[1] + 0.5F);
                            tb = static_cast<std::uint8_t>(texel[2] + 0.5F);
                            ta = static_cast<std::uint8_t>(texel[3] + 0.5F);
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
                            const auto mod = [&](std::uint8_t t, std::size_t k) {
                                const float vc = w0 * ca[k] + w1 * cb[k] + w2 * cc[k];
                                return static_cast<std::uint8_t>(
                                    std::clamp(static_cast<int>(t * vc / 128.0F), 0, 255));
                            };
                            tr = mod(tr, 0U);
                            tg = mod(tg, 1U);
                            tb = mod(tb, 2U);
                            ta = mod(ta, 3U);
                        }
                        if (lit) {
                            const float light =
                                lbase + lgain * (w0 * lights[ia] + w1 * lights[ib] + w2 * lights[ic]);
                            const auto shade = [light](std::uint8_t c) {
                                return static_cast<std::uint8_t>(
                                    std::clamp(static_cast<int>(static_cast<float>(c) * light), 0, 255));
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

    if (floor && view.floor_shadow.size() >= 3U) {
        // Darken each floor pixel once where the footprint lands and the floor
        // is what the camera sees there (the model in front keeps its colour).
        std::vector<std::uint8_t> shadowed(depth.size(), 0U);
        const float tolerance = radius * 0.01F;
        const auto shadow_pixel = [&](int x, int y, std::size_t pi, float z) {
            if (shadowed[pi] != 0U || z > depth[pi] + tolerance) return;
            shadowed[pi] = 1U;
            const auto o = pi * 4U;
            for (std::size_t k = 0U; k < 3U; ++k) {
                image.pixels[o + k] = static_cast<std::uint8_t>(image.pixels[o + k] * 45U / 100U);
            }
            (void)x;
            (void)y;
        };
        for (std::size_t t = 0U; t + 2U < view.floor_shadow.size(); t += 3U) {
            fill(project(view.floor_shadow[t]), project(view.floor_shadow[t + 1U]),
                 project(view.floor_shadow[t + 2U]), shadow_pixel);
        }
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

std::vector<HierarchyScreenPoint> project_hierarchy_points(const Mesh& mesh,
    const HierarchyOverlay& hierarchy, int width, int height, const ViewState& view) {
    std::vector<HierarchyScreenPoint> out;
    if (mesh.vertices.empty() || !hierarchy.available()) return out;

    const int clamped_width = std::clamp(width, 1, 2048);
    const int clamped_height = std::clamp(height, 1, 2048);
    const auto frame = compute_camera_frame(
        view.framing_vertices.empty() ? std::span<const Vec3>{mesh.vertices}
                                      : view.framing_vertices,
        clamped_width, clamped_height);
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
