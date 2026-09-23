#include "dmcresource/view_renderer.h"

#include <algorithm>
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

P3 rotate(const Vec3& v, float yaw, float pitch) {
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

RgbaImage make_canvas(int width, int height) {
    RgbaImage image;
    image.width = std::clamp(width, 1, 2048);
    image.height = std::clamp(height, 1, 2048);
    image.pixels.assign(
        static_cast<std::size_t>(image.width * image.height * 4), 0U);
    for (std::size_t i = 0U; i < image.pixels.size(); i += 4U) {
        image.pixels[i + 0U] = 18U;
        image.pixels[i + 1U] = 18U;
        image.pixels[i + 2U] = 22U;
        image.pixels[i + 3U] = 255U;
    }
    return image;
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
    auto image = make_canvas(width, height);
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

    const bool floor = view.floor && !view.wireframe;
    if (floor) {
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

        const ImagePreview* texture = nullptr;
        if (textured) {
            const auto slot = (*triangle_texture_slots)[t / 3U];
            if (slot != kNoTextureSlot && slot < textures->size() &&
                (*textures)[slot].available()) {
                texture = &(*textures)[slot];
            }
        }

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
                    const auto& uva = mesh.uv0[ia];
                    const auto& uvb = mesh.uv0[ib];
                    const auto& uvc = mesh.uv0[ic];
                    const float u = w0 * uva.u + w1 * uvb.u + w2 * uvc.u;
                    const float v = w0 * uva.v + w1 * uvb.v + w2 * uvc.v;
                    std::uint8_t tr = 0U, tg = 0U, tb = 0U, ta = 0U;
                    if (sample_texture(*texture, u, v, &tr, &tg, &tb, &ta)) {
                        if (ta == 0U) continue;
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
