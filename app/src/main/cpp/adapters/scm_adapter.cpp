#include "dmcresource/adapters/scm_adapter.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <new>
#include <sstream>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "dmc_rengine/formats/scm.hpp"
#include "dmc_rengine/formats/scm_hierarchy.hpp"
#include "dmc_rengine/formats/scm_render.hpp"
#include "dmc_rengine/formats/scm_topology.hpp"
#include "dmc_rengine/formats/scm_transform.hpp"
#include "dmcresource/module_support.h"
#include "dmcresource/uv_projection.h"

namespace dmcresource::adapters {
namespace {

namespace scm = dmc::rengine::formats::scm;

constexpr std::size_t kMaxResourceBytes = 512U * 1024U * 1024U;
constexpr std::size_t kMaxVertices = 2U * 1024U * 1024U;
constexpr std::size_t kMaxIndices = 12U * 1024U * 1024U;

[[nodiscard]] Vec3 add(Vec3 a, Vec3 b) noexcept {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}

[[nodiscard]] Vec3 sub(Vec3 a, Vec3 b) noexcept {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

[[nodiscard]] Vec3 cross(Vec3 a, Vec3 b) noexcept {
    return {
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x,
    };
}

[[nodiscard]] float dot(Vec3 a, Vec3 b) noexcept {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

[[nodiscard]] Vec3 normalize(Vec3 value) noexcept {
    const float len2 = dot(value, value);
    if (!(len2 > 1.0e-12F) || !std::isfinite(len2)) return {};
    const float inv = 1.0F / std::sqrt(len2);
    return {value.x * inv, value.y * inv, value.z * inv};
}

[[nodiscard]] std::string first_error(const scm::ParseResult& parsed) {
    for (const auto& diagnostic : parsed.diagnostics) {
        if (diagnostic.severity == dmc::rengine::formats::ParseSeverity::error) {
            return diagnostic.code + ": " + diagnostic.message;
        }
    }
    if (!parsed.recognized) return "canonical SCM parser did not recognize the payload";
    return "canonical SCM parser rejected the payload";
}

[[nodiscard]] std::string hex32(std::uint32_t value) {
    std::ostringstream out;
    out << "0x" << std::uppercase << std::hex << std::setw(8)
        << std::setfill('0') << value;
    return out.str();
}

[[nodiscard]] std::string hex64(std::uint64_t value) {
    std::ostringstream out;
    out << "0x" << std::uppercase << std::hex << std::setw(16)
        << std::setfill('0') << value;
    return out.str();
}

[[nodiscard]] std::string vec3_text(const scm::Vec3f& value) {
    std::ostringstream out;
    out << value.x << ", " << value.y << ", " << value.z;
    return out.str();
}

[[nodiscard]] Matrix4 to_matrix4(const scm::Matrix4f& source) noexcept {
    Matrix4 out;
    out.values = source.values;
    return out;
}

InspectionNode make_diagnostic_node(
    const dmc::rengine::formats::ParseDiagnostic& diagnostic,
    std::size_t index) {
    InspectionNode node;
    node.id = "diagnostic-" + std::to_string(index);
    node.title = diagnostic.code;
    node.kind = InspectionKind::Diagnostic;
    node.source_span = SourceSpan{diagnostic.offset, 0U};
    node.properties.push_back({
        "Severity",
        std::string{dmc::rengine::formats::to_string(diagnostic.severity)},
        EvidenceLevel::Recognized,
    });
    node.properties.push_back({"Message", diagnostic.message,
                               EvidenceLevel::Recognized});
    return node;
}

[[nodiscard]] bool append_local_mesh(const scm::Mesh& source, Mesh* output) {
    if (output == nullptr) return false;
    const std::size_t vertex_count = source.positions.size();
    if (vertex_count != source.normals.size() ||
        vertex_count != source.uvs.size() ||
        vertex_count != source.colors_topology.size() ||
        vertex_count != source.vertex_count) {
        return false;
    }
    if (vertex_count > kMaxVertices ||
        output->vertices.size() > kMaxVertices - vertex_count) {
        return false;
    }

    const std::size_t base_vertex = output->vertices.size();
    output->vertices.reserve(base_vertex + vertex_count);
    for (const auto& position : source.positions) {
        output->vertices.push_back({position.x, position.y, position.z});
    }
    if (!uv_projection::append_uv0(source.uvs, &output->uv0)) return false;

    if (vertex_count < 3U) return true;
    const auto capacity = (vertex_count - 2U) * 3U;
    if (capacity > kMaxIndices ||
        output->indices.size() > kMaxIndices - capacity) {
        return false;
    }
    output->indices.reserve(output->indices.size() + capacity);

    std::size_t p1 = 0U;
    std::size_t p2 = 1U;
    for (std::size_t p3 = 2U; p3 < vertex_count; ++p3) {
        const bool topology_break =
            (source.colors_topology[p3].topology_flags &
             scm::triangle_break_bit) != 0U;
        if (!topology_break) {
            const Vec3 a{source.positions[p1].x, source.positions[p1].y,
                         source.positions[p1].z};
            const Vec3 b{source.positions[p2].x, source.positions[p2].y,
                         source.positions[p2].z};
            const Vec3 c{source.positions[p3].x, source.positions[p3].y,
                         source.positions[p3].z};
            const Vec3 na{source.normals[p1].x, source.normals[p1].y,
                          source.normals[p1].z};
            const Vec3 nb{source.normals[p2].x, source.normals[p2].y,
                          source.normals[p2].z};
            const Vec3 nc{source.normals[p3].x, source.normals[p3].y,
                          source.normals[p3].z};

            const Vec3 e1 = normalize(sub(c, a));
            const Vec3 e2 = normalize(sub(b, a));
            const Vec3 face = normalize(cross(e1, e2));
            const Vec3 sum = add(add(na, nb), nc);
            const Vec3 normal_sum =
                (sum.x == 0.0F && sum.y == 0.0F && sum.z == 0.0F)
                    ? face
                    : normalize(sum);

            const auto ia = static_cast<std::uint32_t>(base_vertex + p1);
            const auto ib = static_cast<std::uint32_t>(base_vertex + p2);
            const auto ic = static_cast<std::uint32_t>(base_vertex + p3);
            if (dot(normal_sum, face) > 0.0F) {
                output->indices.push_back(ia);
                output->indices.push_back(ic);
                output->indices.push_back(ib);
            } else {
                output->indices.push_back(ia);
                output->indices.push_back(ib);
                output->indices.push_back(ic);
            }
        }
        p1 = p2;
        p2 = p3;
    }
    return true;
}

struct SceneProjection final {
    std::vector<std::int32_t> object_node;
};

[[nodiscard]] bool project_scene(const scm::Document& document,
                                 RenderScene* scene,
                                 InspectionNode* root,
                                 SceneProjection* projection) {
    if (scene == nullptr || root == nullptr || projection == nullptr) return false;

    const auto& source = document.scene_nodes;
    const std::size_t count = document.header.scene_node_count;
    projection->object_node.assign(document.objects.size(), -1);

    InspectionNode hierarchy;
    hierarchy.id = "scene-hierarchy";
    hierarchy.title = "Scene hierarchy";
    hierarchy.kind = InspectionKind::Collection;
    hierarchy.source_span = SourceSpan{source.offset, scm::scene_block_header_size};
    hierarchy.properties.push_back({"NodeCount", std::to_string(count),
                                    EvidenceLevel::StructuralConfirmed});

    if (count == 0U) {
        root->children.push_back(std::move(hierarchy));
        return document.objects.empty();
    }

    if (source.parent_by_order_position.size() != count ||
        source.node_at_order_position.size() != count ||
        source.object_binding_by_node_index.size() != count ||
        source.transform_by_node_index.size() != count) {
        return false;
    }

    const auto world = scm::build_world_matrices(source);
    if (!world.has_value() || world->size() != count) return false;

    std::vector<std::int32_t> parent_by_node(count, -1);
    for (std::size_t order_position = 0U;
         order_position < count;
         ++order_position) {
        const auto node = static_cast<std::size_t>(
            source.node_at_order_position[order_position]);
        if (node >= count) return false;
        parent_by_node[node] = source.parent_by_order_position[order_position];

        const auto binding = source.object_binding_by_node_index[node];
        if (binding >= 0) {
            const auto object_index = static_cast<std::size_t>(binding);
            if (object_index >= document.objects.size()) return false;
            projection->object_node[object_index] =
                static_cast<std::int32_t>(node);
        }
    }

    scene->nodes.reserve(scene->nodes.size() + count);
    hierarchy.children.reserve(count);
    for (std::size_t node_index = 0U; node_index < count; ++node_index) {
        const auto& transform = source.transform_by_node_index[node_index];
        RenderNode render_node;
        render_node.name = "SCM Node " + std::to_string(node_index);
        render_node.kind = RenderNodeKind::Scene;
        render_node.parent = parent_by_node[node_index];
        render_node.spatial_authority = true;
        render_node.local = to_matrix4(scm::build_local_transform(transform));
        render_node.world = to_matrix4((*world)[node_index]);
        scene->nodes.push_back(std::move(render_node));

        InspectionNode node;
        node.id = "scene-node-" + std::to_string(node_index);
        node.title = "Node " + std::to_string(node_index);
        node.kind = InspectionKind::Node;
        node.properties.push_back({"Parent", std::to_string(parent_by_node[node_index]),
                                   EvidenceLevel::ExeAndCorpusConfirmed});
        node.properties.push_back({
            "ObjectBinding",
            std::to_string(static_cast<int>(
                source.object_binding_by_node_index[node_index])),
            EvidenceLevel::ExeAndCorpusConfirmed,
        });

        InspectionNode transform_node;
        transform_node.id = node.id + "-transform";
        transform_node.title = "Transform";
        transform_node.kind = InspectionKind::Transform;
        transform_node.properties.push_back({"Translation", vec3_text(transform.translation),
                                             EvidenceLevel::ExeConfirmed});
        transform_node.properties.push_back({"TranslationMagnitude",
                                             std::to_string(transform.translation_magnitude),
                                             EvidenceLevel::DataConfirmed});
        transform_node.properties.push_back({"RotationXYZRadians",
                                             vec3_text(transform.rotation_xyz_radians),
                                             EvidenceLevel::ExeConfirmed});
        transform_node.properties.push_back({
            "WorldTranslation",
            std::to_string((*world)[node_index](3U, 0U)) + ", " +
                std::to_string((*world)[node_index](3U, 1U)) + ", " +
                std::to_string((*world)[node_index](3U, 2U)),
            EvidenceLevel::ExeConfirmed,
        });
        node.children.push_back(std::move(transform_node));
        hierarchy.children.push_back(std::move(node));
    }

    root->children.push_back(std::move(hierarchy));
    return true;
}

}  // namespace

PipelineResult run_scm_adapter(const ProbeResult& probe,
                               const std::uint8_t* bytes,
                               std::size_t size,
                               const char* module_id) noexcept {
    if (size > kMaxResourceBytes) {
        return module_support::reject(probe, module_id,
                                      "SCM rejected: resource exceeds 512 MiB reader cap");
    }
    if (size != 0U && bytes == nullptr) {
        return module_support::reject(probe, module_id,
                                      "SCM rejected: null input span");
    }

    try {
        const auto byte_span = std::span<const std::byte>{
            reinterpret_cast<const std::byte*>(bytes), size};
        const auto parsed = scm::Parser::parse(byte_span);
        if (!parsed.ok()) {
            return module_support::reject(
                probe, module_id,
                "SCM rejected by canonical parser: " + first_error(parsed));
        }

        PipelineResult out;
        out.accepted = true;
        out.probe = probe;
        out.modules.push_back({"identity-probe", true});
        out.modules.push_back({"bounded-read-guard", true});
        out.modules.push_back({"canonical.scm.structural-parser", true});
        out.modules.push_back({"canonical.scm.scene-hierarchy", true});
        out.modules.push_back({module_id, true});

        out.inspection.format = "SCM";
        out.inspection.root.id = "scm";
        out.inspection.root.title = "SCM";
        out.inspection.root.kind = InspectionKind::Document;
        out.inspection.root.source_span = SourceSpan{0U, size};

        InspectionNode header;
        header.id = "header";
        header.title = "Header";
        header.kind = InspectionKind::Header;
        header.source_span = SourceSpan{0U, scm::header_size};
        header.properties.push_back({"Version",
                                     std::to_string(parsed.document.header.version),
                                     EvidenceLevel::StructuralConfirmed});
        header.properties.push_back({"ObjectCount",
                                     std::to_string(parsed.document.header.object_count),
                                     EvidenceLevel::StructuralConfirmed});
        header.properties.push_back({"SceneNodeCount",
                                     std::to_string(parsed.document.header.scene_node_count),
                                     EvidenceLevel::StructuralConfirmed});
        header.properties.push_back({"TextureSlotCount",
                                     std::to_string(parsed.document.header.texture_slot_count),
                                     EvidenceLevel::StructuralConfirmed});
        header.properties.push_back({"LegacyResourceCode",
                                     std::to_string(parsed.document.header.resource_code.raw),
                                     EvidenceLevel::ExeAndCorpusConfirmed});
        header.properties.push_back({"ResourceFamilyClass",
                                     std::to_string(parsed.document.header.resource_code.family_class),
                                     EvidenceLevel::ExeAndCorpusConfirmed});
        header.properties.push_back({"ResourceModelSet",
                                     std::to_string(parsed.document.header.resource_code.model_set),
                                     EvidenceLevel::ExeAndCorpusConfirmed});
        header.properties.push_back({"ResourceSubIndex",
                                     std::to_string(parsed.document.header.resource_code.sub_index),
                                     EvidenceLevel::ExeAndCorpusConfirmed});
        out.inspection.root.children.push_back(std::move(header));

        SceneProjection scene_projection;
        if (!project_scene(parsed.document, &out.scene,
                           &out.inspection.root, &scene_projection)) {
            return module_support::reject(
                probe, module_id,
                "SCM canonical scene projection failed despite accepted parser state");
        }

        InspectionNode objects;
        objects.id = "objects";
        objects.title = "Objects";
        objects.kind = InspectionKind::Collection;
        objects.children.reserve(parsed.document.objects.size());

        std::size_t total_meshes = 0U;
        std::size_t total_vertices = 0U;
        std::size_t total_triangles = 0U;

        for (std::size_t object_index = 0U;
             object_index < parsed.document.objects.size();
             ++object_index) {
            const auto& object = parsed.document.objects[object_index];
            InspectionNode object_node;
            object_node.id = "object-" + std::to_string(object_index);
            object_node.title = "Object " + std::to_string(object_index);
            object_node.kind = InspectionKind::Object;
            object_node.source_span = SourceSpan{object.record_offset,
                                                 scm::object_record_size};
            object_node.properties.push_back({"MeshCount",
                                              std::to_string(object.meshes.size()),
                                              EvidenceLevel::StructuralConfirmed});
            object_node.properties.push_back({"TotalVertices",
                                              std::to_string(object.total_vertex_count),
                                              EvidenceLevel::StructuralConfirmed});
            object_node.properties.push_back({"Flags", hex32(object.flags),
                                              EvidenceLevel::ExeConfirmed});
            object_node.properties.push_back({"AlphaControl",
                                              std::to_string(object.alpha_control),
                                              EvidenceLevel::ExeConfirmed});
            object_node.properties.push_back({"BoundingCenter",
                                              vec3_text(object.bounding_center),
                                              EvidenceLevel::ExeAndCorpusConfirmed});
            object_node.properties.push_back({"BoundingRadius",
                                              std::to_string(object.bounding_radius),
                                              EvidenceLevel::ExeAndCorpusConfirmed});
            object_node.properties.push_back({
                "SceneNodeBinding",
                std::to_string(scene_projection.object_node[object_index]),
                EvidenceLevel::ExeAndCorpusConfirmed,
            });

            const auto filter_state =
                scm::legacy_gs_tex1_filter_from_object_flags(object.flags);
            InspectionNode object_material;
            object_material.id = object_node.id + "-material-state";
            object_material.title = "Object material state";
            object_material.kind = InspectionKind::MaterialState;
            object_material.properties.push_back({
                "LegacyGsTex1",
                hex64(filter_state),
                EvidenceLevel::ExeConfirmed,
            });
            object_material.properties.push_back({
                "TextureFilter",
                filter_state == scm::legacy_gs_tex1_nearest_filter
                    ? "nearest"
                    : "linear",
                EvidenceLevel::ExeConfirmed,
            });
            object_node.children.push_back(std::move(object_material));

            for (std::size_t mesh_index = 0U;
                 mesh_index < object.meshes.size();
                 ++mesh_index) {
                const auto& source = object.meshes[mesh_index];
                ++total_meshes;
                total_vertices += source.positions.size();

                MeshPrimitive primitive;
                primitive.name = "Object " + std::to_string(object_index) +
                                 " / Mesh " + std::to_string(mesh_index);
                primitive.object_index = static_cast<std::uint32_t>(object_index);
                primitive.mesh_index = static_cast<std::uint32_t>(mesh_index);
                primitive.node_index = scene_projection.object_node[object_index];
                if (!append_local_mesh(source, &primitive.mesh)) {
                    return module_support::reject(
                        probe, module_id,
                        "SCM canonical local-mesh projection exceeded topology/UV/size limits");
                }
                total_triangles += primitive.mesh.indices.size() / 3U;

                const auto primitive_index =
                    static_cast<std::uint32_t>(out.scene.meshes.size());
                out.scene.textures.push_back(TextureBinding{
                    primitive_index,
                    source.texture_index,
                    "SCM external texture companion",
                });

                InspectionNode mesh_node;
                mesh_node.id = object_node.id + "-mesh-" +
                               std::to_string(mesh_index);
                mesh_node.title = "Mesh " + std::to_string(mesh_index);
                mesh_node.kind = InspectionKind::Mesh;
                mesh_node.source_span = SourceSpan{source.record_offset,
                                                   scm::mesh_record_size};
                mesh_node.properties.push_back({"Vertices",
                                                std::to_string(source.positions.size()),
                                                EvidenceLevel::StructuralConfirmed});
                mesh_node.properties.push_back({"Triangles",
                                                std::to_string(primitive.mesh.indices.size() / 3U),
                                                EvidenceLevel::DataConfirmed});
                mesh_node.properties.push_back({"Normals",
                                                std::to_string(source.normals.size()),
                                                EvidenceLevel::StructuralConfirmed});
                mesh_node.properties.push_back({"UVs",
                                                std::to_string(source.uvs.size()),
                                                EvidenceLevel::StructuralConfirmed});
                mesh_node.properties.push_back({"TextureIndex",
                                                std::to_string(source.texture_index),
                                                EvidenceLevel::ExeConfirmed});
                mesh_node.properties.push_back({"TopologyFlagMask",
                                                hex32(source.observed_topology_flag_mask),
                                                EvidenceLevel::DataConfirmed});

                InspectionNode texture;
                texture.id = mesh_node.id + "-texture";
                texture.title = "Texture binding";
                texture.kind = InspectionKind::Texture;
                texture.properties.push_back({"Slot",
                                              std::to_string(source.texture_index),
                                              EvidenceLevel::ExeConfirmed});
                texture.properties.push_back({"Source",
                                              "external texture companion",
                                              EvidenceLevel::ExeConfirmed});
                mesh_node.children.push_back(std::move(texture));

                InspectionNode sampler;
                sampler.id = mesh_node.id + "-sampler";
                sampler.title = "Legacy GS sampler";
                sampler.kind = InspectionKind::MaterialState;
                sampler.properties.push_back({"ClampMinU",
                                              std::to_string(source.gs_clamp_region_repeat.min_u),
                                              EvidenceLevel::ExeConfirmed});
                sampler.properties.push_back({"ClampMaxU",
                                              std::to_string(source.gs_clamp_region_repeat.max_u),
                                              EvidenceLevel::ExeConfirmed});
                sampler.properties.push_back({"ClampMinV",
                                              std::to_string(source.gs_clamp_region_repeat.min_v),
                                              EvidenceLevel::ExeConfirmed});
                sampler.properties.push_back({"ClampMaxV",
                                              std::to_string(source.gs_clamp_region_repeat.max_v),
                                              EvidenceLevel::ExeConfirmed});
                sampler.properties.push_back({
                    "PackedRegionRepeat",
                    hex64(scm::pack_legacy_gs_clamp_region_repeat(
                        source.gs_clamp_region_repeat)),
                    EvidenceLevel::ExeConfirmed,
                });
                mesh_node.children.push_back(std::move(sampler));

                object_node.children.push_back(std::move(mesh_node));
                out.scene.meshes.push_back(std::move(primitive));
            }
            objects.children.push_back(std::move(object_node));
        }
        out.modules.push_back({"native.uv-projection", true});
        out.inspection.root.children.push_back(std::move(objects));

        if (!parsed.diagnostics.empty()) {
            InspectionNode diagnostics;
            diagnostics.id = "diagnostics";
            diagnostics.title = "Diagnostics";
            diagnostics.kind = InspectionKind::Collection;
            diagnostics.children.reserve(parsed.diagnostics.size());
            for (std::size_t index = 0U;
                 index < parsed.diagnostics.size();
                 ++index) {
                diagnostics.children.push_back(
                    make_diagnostic_node(parsed.diagnostics[index], index));
            }
            out.inspection.root.children.push_back(std::move(diagnostics));
        }

        out.renderable = out.scene.has_geometry();
        std::ostringstream detail;
        detail << "SCM canonical C++20 reader"
               << " | objects=" << parsed.document.objects.size()
               << " meshes=" << total_meshes
               << " vertices=" << total_vertices
               << " triangles=" << total_triangles
               << " nodes=" << out.scene.nodes.size()
               << " textures=" << out.scene.textures.size()
               << " diagnostics=" << parsed.diagnostics.size();
        out.detail = detail.str();
        return out;
    } catch (const std::bad_alloc&) {
        return module_support::reject(probe, module_id,
                                      "SCM rejected: allocation failed");
    } catch (...) {
        return module_support::reject(probe, module_id,
                                      "SCM rejected: unexpected canonical adapter failure");
    }
}

}  // namespace dmcresource::adapters
