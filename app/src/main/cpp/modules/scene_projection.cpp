#include "dmcresource/resource_limits.h"
#include "dmcresource/scene_projection.h"
#include <cmath>
#include <limits>
#include <new>
#include <utility>

namespace dmcresource {
namespace {
using namespace dmcresource::resource_limits;

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


}  // namespace

void append_vertex_channels(const Mesh& source, bool color0, bool blend0, Mesh* out,
                            bool normal0) {
    if (out == nullptr) return;
    const auto count = source.vertices.size();
    if (normal0) {
        if (source.has_normal0()) {
            out->normal0.insert(out->normal0.end(), source.normal0.begin(), source.normal0.end());
        } else {
            out->normal0.insert(out->normal0.end(), count, Vec3{});
        }
    }
    if (color0) {
        if (source.has_color0()) {
            out->color0.insert(out->color0.end(), source.color0.begin(), source.color0.end());
        } else {
            out->color0.insert(out->color0.end(), count,
                               std::array<std::uint8_t, 4>{0x80U, 0x80U, 0x80U, 0x80U});
        }
    }
    if (blend0) {
        if (source.has_blend0()) {
            out->blend0.insert(out->blend0.end(), source.blend0.begin(), source.blend0.end());
        } else {
            out->blend0.insert(out->blend0.end(), count, std::uint8_t{0U});
        }
    }
}

bool materialize_render_scene(const RenderScene& scene, Mesh* out) noexcept {
    if (out == nullptr) return false;
    try {
        Mesh materialized;
        std::size_t total_vertices = 0U;
        std::size_t total_indices = 0U;
        bool complete_uv0 = !scene.meshes.empty();
        for (const auto& primitive : scene.meshes) {
            if (primitive.mesh.vertices.size() > kMaxVertices - total_vertices ||
                primitive.mesh.indices.size() > kMaxIndices - total_indices) {
                return false;
            }
            total_vertices += primitive.mesh.vertices.size();
            total_indices += primitive.mesh.indices.size();
            if (!primitive.mesh.has_uv0()) complete_uv0 = false;
        }
        materialized.vertices.reserve(total_vertices);
        materialized.indices.reserve(total_indices);
        if (complete_uv0) materialized.uv0.reserve(total_vertices);
        bool any_color0 = false;
        bool any_blend0 = false;
        bool any_normal0 = false;
        for (const auto& primitive : scene.meshes) {
            any_normal0 = any_normal0 || primitive.mesh.has_normal0();
            any_color0 = any_color0 || primitive.mesh.has_color0();
            any_blend0 = any_blend0 || primitive.mesh.has_blend0();
        }

        for (const auto& primitive : scene.meshes) {
            const Matrix4* world = nullptr;
            if (primitive.node_index >= 0) {
                const auto node_index = static_cast<std::size_t>(primitive.node_index);
                if (node_index >= scene.nodes.size()) return false;
                world = &scene.nodes[node_index].world;
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
            append_vertex_channels(primitive.mesh, any_color0, any_blend0, &materialized, any_normal0);

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
        }

        *out = std::move(materialized);
        return true;
    } catch (const std::bad_alloc&) {
        return false;
    } catch (...) {
        return false;
    }
}

bool materialize_triangle_texture_slots(
        const RenderScene& scene,
        std::vector<std::uint32_t>* out) noexcept {
    if (out == nullptr) return false;
    try {
        std::vector<std::uint32_t> slots;
        std::size_t total_triangles = 0U;
        for (const auto& primitive : scene.meshes) {
            if (primitive.mesh.indices.size() % 3U != 0U) return false;
            const auto triangles = primitive.mesh.indices.size() / 3U;
            if (triangles > kMaxIndices / 3U - total_triangles) return false;
            total_triangles += triangles;
        }
        slots.reserve(total_triangles);

        for (std::size_t primitive_index = 0U;
             primitive_index < scene.meshes.size();
             ++primitive_index) {
            const auto& primitive = scene.meshes[primitive_index];
            std::uint32_t slot = kNoTextureSlot;
            bool binding_seen = false;
            for (const auto& binding : scene.textures) {
                if (binding.mesh_primitive != primitive_index) continue;
                if (binding_seen && slot != binding.texture_slot) return false;
                slot = binding.texture_slot;
                binding_seen = true;
            }
            slots.insert(slots.end(), primitive.mesh.indices.size() / 3U, slot);
        }

        if (slots.size() != total_triangles) return false;
        *out = std::move(slots);
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


}  // namespace dmcresource
