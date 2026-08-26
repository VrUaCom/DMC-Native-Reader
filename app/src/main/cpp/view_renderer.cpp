#include "dmcresource/view_renderer.h"

#include <algorithm>
#include <cmath>
#include <limits>
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

void put_pixel(RgbaImage& image, int x, int y, std::uint8_t shade) {
    if (x < 0 || y < 0 || x >= image.width || y >= image.height) return;
    const auto o = static_cast<std::size_t>(y * image.width + x) * 4;
    image.pixels[o + 0] = shade;
    image.pixels[o + 1] = shade;
    image.pixels[o + 2] = static_cast<std::uint8_t>(std::min(255, shade + 10));
    image.pixels[o + 3] = 255;
}

void line(RgbaImage& image, P2 a, P2 b) {
    int x0 = static_cast<int>(std::lround(a.x));
    int y0 = static_cast<int>(std::lround(a.y));
    const int x1 = static_cast<int>(std::lround(b.x));
    const int y1 = static_cast<int>(std::lround(b.y));
    const int dx = std::abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    const int dy = -std::abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        put_pixel(image, x0, y0, 235);
        if (x0 == x1 && y0 == y1) break;
        const int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

}  // namespace

RgbaImage render_view(const Mesh& mesh, int width, int height, const ViewState& view) {
    RgbaImage image;
    image.width = std::clamp(width, 1, 2048);
    image.height = std::clamp(height, 1, 2048);
    image.pixels.assign(static_cast<std::size_t>(image.width * image.height * 4), 0);
    for (std::size_t i = 0; i < image.pixels.size(); i += 4) {
        image.pixels[i + 0] = 18;
        image.pixels[i + 1] = 18;
        image.pixels[i + 2] = 22;
        image.pixels[i + 3] = 255;
    }
    if (mesh.vertices.empty() || mesh.indices.size() < 3) return image;

    Vec3 center{};
    for (const auto& v : mesh.vertices) {
        center.x += v.x; center.y += v.y; center.z += v.z;
    }
    const float inv_n = 1.0f / static_cast<float>(mesh.vertices.size());
    center.x *= inv_n; center.y *= inv_n; center.z *= inv_n;

    float radius = 1e-4f;
    for (const auto& v : mesh.vertices) {
        const float dx = v.x - center.x, dy = v.y - center.y, dz = v.z - center.z;
        radius = std::max(radius, std::sqrt(dx*dx + dy*dy + dz*dz));
    }

    const float zoom = std::clamp(view.zoom, 0.15f, 8.0f);
    const float scale = 0.42f * static_cast<float>(std::min(image.width, image.height)) * zoom / radius;

    std::vector<P2> p;
    p.reserve(mesh.vertices.size());
    for (const auto& v : mesh.vertices) {
        const Vec3 local{v.x - center.x, v.y - center.y, v.z - center.z};
        const auto r = rotate(local, view.yaw_radians, view.pitch_radians);
        p.push_back({image.width * 0.5f + r.x * scale,
                     image.height * 0.5f - r.y * scale,
                     r.z});
    }

    std::vector<float> depth(static_cast<std::size_t>(image.width * image.height),
                             std::numeric_limits<float>::infinity());

    for (std::size_t t = 0; t + 2 < mesh.indices.size(); t += 3) {
        const auto ia = mesh.indices[t], ib = mesh.indices[t + 1], ic = mesh.indices[t + 2];
        if (ia >= p.size() || ib >= p.size() || ic >= p.size()) continue;
        const P2 a = p[ia], b = p[ib], c = p[ic];
        if (view.wireframe) {
            line(image, a, b); line(image, b, c); line(image, c, a);
            continue;
        }
        const float area = edge(a, b, c.x, c.y);
        if (std::fabs(area) < 1e-6f) continue;
        const int x0 = std::max(0, static_cast<int>(std::floor(std::min({a.x,b.x,c.x}))));
        const int y0 = std::max(0, static_cast<int>(std::floor(std::min({a.y,b.y,c.y}))));
        const int x1 = std::min(image.width - 1, static_cast<int>(std::ceil(std::max({a.x,b.x,c.x}))));
        const int y1 = std::min(image.height - 1, static_cast<int>(std::ceil(std::max({a.y,b.y,c.y}))));
        for (int y = y0; y <= y1; ++y) {
            for (int x = x0; x <= x1; ++x) {
                const float px = x + 0.5f, py = y + 0.5f;
                const float w0 = edge(b, c, px, py) / area;
                const float w1 = edge(c, a, px, py) / area;
                const float w2 = edge(a, b, px, py) / area;
                if (w0 < 0.f || w1 < 0.f || w2 < 0.f) continue;
                const float z = w0*a.z + w1*b.z + w2*c.z;
                const auto pi = static_cast<std::size_t>(y * image.width + x);
                if (z >= depth[pi]) continue;
                depth[pi] = z;
                const float zn = 0.5f + 0.5f * std::tanh(-z / radius);
                const auto shade = static_cast<std::uint8_t>(145 + 80 * zn);
                put_pixel(image, x, y, shade);
            }
        }
    }
    return image;
}

}  // namespace dmcresource
