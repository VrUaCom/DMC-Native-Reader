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

constexpr std::size_t kMaxSceneVertices = 2U * 1024U * 1024U;
constexpr std::size_t kMaxSceneIndices = 12U * 1024U * 1024U;

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

[[nodiscard]] bool transform_point_row_vector(const Vec3& source,
                                              const Matrix4& matrix,
                                              Vec3* out) noexcept {
    if (out == nullptr) return false;
    const auto& m = matrix.values;
    const float x = source.x * m[0] + source.y * m[4] +
                    source.z * m[8] + m[12];
    const float y = source.x * m[1] + source.y * m[5] +
                    source.z * m[9] + m[13];
    const float z = source.x * m[2] + source.y * m[6] +
                    source.z * m[10] + m[14];
    const float w = source.x * m[3] + source.y * m[7] +
                    source.z * m[11] + m[15];
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z) ||
        !std::isfinite(w) || std::fabs(w - 1.0F) > 0.0001F) {
        return false;
    }
    *out = {x, y, z};
    return true;
}

[[nodiscard]] bool finite_matrix_translation(const Matrix4& matrix,
                                             Vec3* out) noexcept {
    if (out == nullptr) return false;
    const auto& m = matrix.values;
    for (const float value : m) {
        if (!std::isfinite(value)) return false;
    }
    if (std::fabs(m[15] - 1.0F) > 0.0001F) return false;
    *out = {m[12], m[13], m[14]};
    return true;
}

void put_pixel(RgbaImage& image, int x, int y, std::uint8_t shade) {
    if (x < 0 || y < 0 || x >= image.width || y >= image.height) return;
    const auto o = static_cast<std::size_t>(y * image.width + x) * 4;
    image.pixels[o + 0] = shade;
    image.pixels[o + 1] = shade;
    image.pixels[o + 2] = static_cast<std::uint8_t>(std::min(255, shade + 10));
    image.pixels[o + 3] = 255;
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

void render_uv_layout(const Mesh& mesh, const ViewState& view,
                      RgbaImage* image) {
    if (image == nullptr || !mesh.has_uv0() || mesh.indices.size() < 3U) return;

    // Always keep the canonical 0..1 tile visible while expanding to cover
    // observed coordinates outside that tile (valid for REGION_REPEAT assets).
    float min_u = 0.0F;
    float min_v = 0.0F;
    float max_u = 1.0F;
    float max_v = 1.0F;
    for (const auto& uv : mesh.uv0) {
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
    const float usable_w = std::max(1.0F, static_cast<float>(image->width) - 2.0F * padding);
    const float usable_h = std::max(1.0F, static_cast<float>(image->height) - 2.0F * padding);
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

    std::vector<P2> points;
    points.reserve(mesh.uv0.size());
    for (const auto& uv : mesh.uv0) points.push_back(map_uv(uv.u, uv.v));

    for (std::size_t t = 0U; t + 2U < mesh.indices.size(); t += 3U) {
        const auto ia = mesh.indices[t + 0U];
        const auto ib = mesh.indices[t + 1U];
        const auto ic = mesh.indices[t + 2U];
        if (ia >= points.size() || ib >= points.size() || ic >= points.size()) continue;
        line(*image, points[ia], points[ib]);
        line(*image, points[ib], points[ic]);
        line(*image, points[ic], points[ia]);
    }
}

}  // namespace

bool materialize_render_scene(const RenderScene& scene, Mesh* out) noexcept {
    if (out == nullptr) return false;
    try {
        Mesh materialized;
        std::size_t total_vertices = 0U;
        std::size_t total_indices = 0U;
        bool complete_uv0 = !scene.meshes.empty();
        for (const auto& primitive : scene.meshes) {
            if (primitive.mesh.vertices.size() > kMaxSceneVertices - total_vertices ||
                primitive.mesh.indices.size() > kMaxSceneIndices - total_indices ||
                primitive.mesh.indices.size() % 3U != 0U) {
                return false;
            }
            total_vertices += primitive.mesh.vertices.size();
            total_indices += primitive.mesh.indices.size();
            if (!primitive.mesh.has_uv0()) complete_uv0 = false;
        }
        materialized.vertices.reserve(total_vertices);
        materialized.indices.reserve(total_indices);
        materialized.triangle_texture_slots.reserve(total_indices / 3U);
        if (complete_uv0) materialized.uv0.reserve(total_vertices);

        for (std::size_t primitive_index = 0U;
             primitive_index < scene.meshes.size();
             ++primitive_index) {
            const auto& primitive = scene.meshes[primitive_index];
            const Matrix4* world = nullptr;
            if (primitive.node_index >= 0) {
                const auto node_index = static_cast<std::size_t>(primitive.node_index);
                if (node_index >= scene.nodes.size()) return false;
                world = &scene.nodes[node_index].world;
            }

            std::uint32_t texture_slot = Mesh::kNoTextureSlot;
            bool binding_seen = false;
            for (const auto& binding : scene.textures) {
                if (binding.mesh_primitive != primitive_index) continue;
                if (binding_seen && texture_slot != binding.texture_slot) return false;
                texture_slot = binding.texture_slot;
                binding_seen = true;
            }

            const std::size_t base = materialized.vertices.size();
            for (const auto& vertex : primitive.mesh.vertices) {
                Vec3 projected = vertex;
                if (world != nullptr &&
                    !transform_point_row_vector(vertex, *world, &projected)) {
                    return false;
                }
                materialized.vertices.push_back(projected);
            }
            if (complete_uv0) {
                materialized.uv0.insert(
                    materialized.uv0.end(),
                    primitive.mesh.uv0.begin(),
                    primitive.mesh.uv0.end());
            }

            if (base > static_cast<std::size_t>(
                           std::numeric_limits<std::uint32_t>::max())) {
                return false;
            }
            const auto base32 = static_cast<std::uint32_t>(base);
            for (const auto index : primitive.mesh.indices) {
                if (index >= primitive.mesh.vertices.size() ||
                    index > std::numeric_limits<std::uint32_t>::max() - base32) {
                    return false;
                }
                materialized.indices.push_back(base32 + index);
            }
            const auto triangle_count = primitive.mesh.indices.size() / 3U;
            materialized.triangle_texture_slots.insert(
                materialized.triangle_texture_slots.end(),
                triangle_count, texture_slot);
        }

        *out = std::move(materialized);
        return true;
    } catch (const std::bad_alloc&) {
        return false;
    } catch (...) {
        return false;
    }
}

bool materialize_hierarchy_overlay(const RenderScene& scene,
                                   HierarchyOverlay* out) noexcept {
    if (out == nullptr) return false;
    try {
        HierarchyOverlay overlay;
        overlay.points.reserve(scene.nodes.size());
        overlay.kinds.reserve(scene.nodes.size());
        overlay.edges.reserve(scene.nodes.size());

        bool spatial_authority = !scene.nodes.empty();
        for (std::size_t index = 0U; index < scene.nodes.size(); ++index) {
            const auto& node = scene.nodes[index];
            Vec3 point;
            if (!finite_matrix_translation(node.world, &point)) return false;
            overlay.points.push_back(point);
            overlay.kinds.push_back(node.kind);
            if (!node.spatial_authority) spatial_authority = false;

            if (node.parent >= 0) {
                const auto parent = static_cast<std::size_t>(node.parent);
                if (parent >= scene.nodes.size() || parent == index) return false;
                overlay.edges.push_back({static_cast<std::uint32_t>(parent),
                                         static_cast<std::uint32_t>(index)});
            }
        }

        for (std::size_t start = 0U; start < scene.nodes.size(); ++start) {
            std::size_t current = start;
            std::size_t hops = 0U;
            while (scene.nodes[current].parent >= 0) {
                const auto parent = static_cast<std::size_t>(scene.nodes[current].parent);
                if (parent >= scene.nodes.size()) return false;
                ++hops;
                if (hops > scene.nodes.size()) return false;
                current = parent;
            }
        }

        for (const auto& edge_value : overlay.edges) {
            if (edge_value.parent >= overlay.points.size() ||
                edge_value.child >= overlay.points.size()) {
                return false;
            }
        }

        overlay.spatial = spatial_authority;
        *out = std::move(overlay);
        return true;
    } catch (const std::bad_alloc&) {
        return false;
    } catch (...) {
        return false;
    }
}

RgbaImage render_view(const Mesh& mesh, int width, int height,
                      const ViewState& view,
                      const HierarchyOverlay* hierarchy,
                      const std::vector<ImagePreview>* textures) {
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

    if (view.uv_layout) {
        render_uv_layout(mesh, view, &image);
        return image;
    }

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

    const bool textured = textures != nullptr && mesh.has_uv0() &&
                          mesh.has_triangle_texture_slots();
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

        const ImagePreview* texture = nullptr;
        if (textured) {
            const auto triangle = t / 3U;
            const auto slot = mesh.triangle_texture_slots[triangle];
            if (slot != Mesh::kNoTextureSlot && slot < textures->size() &&
                (*textures)[slot].available()) {
                texture = &(*textures)[slot];
            }
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
                const float zn = 0.5f + 0.5f * std::tanh(-z / radius);
                const auto shade = static_cast<std::uint8_t>(145 + 80 * zn);
                put_pixel(image, x, y, shade);
            }
        }
    }

    if (hierarchy != nullptr && hierarchy->available()) {
        std::vector<P2> hp;
        hp.reserve(hierarchy->points.size());
        for (const auto& point : hierarchy->points) {
            const Vec3 local{point.x - center.x, point.y - center.y, point.z - center.z};
            const auto r = rotate(local, view.yaw_radians, view.pitch_radians);
            hp.push_back({image.width * 0.5f + r.x * scale,
                          image.height * 0.5f - r.y * scale,
                          r.z});
        }
        for (const auto& edge_value : hierarchy->edges) {
            if (edge_value.parent >= hp.size() || edge_value.child >= hp.size()) continue;
            line(image, hp[edge_value.parent], hp[edge_value.child], 255);
        }
        for (const auto& point : hp) marker(image, point, 255);
    }

    return image;
}

}  // namespace dmcresource
