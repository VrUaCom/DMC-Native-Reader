#include "dmcresource/adapters/mod_adapter.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <new>
#include <sstream>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "dmc_rengine/formats/mod.hpp"
#include "dmc_rengine/formats/mod/world_transform.hpp"
#include "dmcresource/module_support.h"

namespace dmcresource::adapters {
namespace {

constexpr std::size_t kMaxResourceBytes = 512U * 1024U * 1024U;
constexpr std::size_t kMaxVertices = 2U * 1024U * 1024U;
constexpr std::size_t kMaxIndices = 12U * 1024U * 1024U;

using CanonicalParseResult = dmc::rengine::formats::mod::ParseResult;
using CanonicalMesh = dmc::rengine::formats::mod::InnerMesh;
using ParseSeverity = dmc::rengine::formats::ParseSeverity;
namespace CanonicalWorld = dmc::rengine::formats::mod::world_transform;

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

[[nodiscard]] Vec3 normalize(Vec3 v) noexcept {
    const float len2 = dot(v, v);
    if (!std::isfinite(len2) || len2 <= 1.0e-12F) return {};
    const float inv = 1.0F / std::sqrt(len2);
    return {v.x * inv, v.y * inv, v.z * inv};
}

[[nodiscard]] bool append_legacy_compatible_topology(const CanonicalMesh& source,
                                                     Mesh* out) {
    if (out == nullptr) return false;
    const std::size_t vc = source.positions.size();
    if (vc != source.normals.size() || vc != source.control_words.size()) return false;
    if (vc > kMaxVertices || out->vertices.size() > kMaxVertices - vc) return false;

    const std::size_t base = out->vertices.size();
    out->vertices.reserve(base + vc);
    for (const auto& p : source.positions) {
        out->vertices.push_back({p.x, p.y, p.z});
    }

    if (vc < 3U) return true;
    const auto triangle_capacity = (vc - 2U) * 3U;
    if (triangle_capacity > kMaxIndices ||
        out->indices.size() > kMaxIndices - triangle_capacity) {
        return false;
    }
    out->indices.reserve(out->indices.size() + triangle_capacity);

    std::size_t p1 = 0U;
    std::size_t p2 = 1U;
    for (std::size_t p3 = 2U; p3 < vc; ++p3) {
        const bool topology_break = (source.control_words[p3] & 0x8000U) != 0U;
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

            const auto ia = static_cast<std::uint32_t>(base + p1);
            const auto ib = static_cast<std::uint32_t>(base + p2);
            const auto ic = static_cast<std::uint32_t>(base + p3);
            if (dot(normal_sum, face) > 0.0F) {
                out->indices.push_back(ia);
                out->indices.push_back(ic);
                out->indices.push_back(ib);
            } else {
                out->indices.push_back(ia);
                out->indices.push_back(ib);
                out->indices.push_back(ic);
            }
        }
        p1 = p2;
        p2 = p3;
    }
    return true;
}

[[nodiscard]] std::string first_error(const CanonicalParseResult& parsed) {
    for (const auto& diagnostic : parsed.diagnostics) {
        if (diagnostic.severity == ParseSeverity::error) {
            return diagnostic.code + ": " + diagnostic.message;
        }
    }
    if (!parsed.recognized) return "canonical MOD parser did not recognize the payload";
    return "canonical MOD parser rejected the payload";
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

[[nodiscard]] Matrix4 to_render_matrix(
    const CanonicalWorld::Matrix4f& source) noexcept {
    Matrix4 result;
    result.values = source.values;
    return result;
}

[[nodiscard]] std::string vec3_text(float x, float y, float z) {
    std::ostringstream out;
    out << x << ", " << y << ", " << z;
    return out.str();
}

[[nodiscard]] bool project_hierarchy(
    const dmc::rengine::formats::mod::Document& document,
    RenderScene* scene,
    InspectionNode* root) {
    if (scene == nullptr || root == nullptr) return false;

    const auto& domain = document.transform_domain;
    const std::size_t count = document.header.transform_domain_count;

    bool hierarchy_mapping_valid =
        domain.permutation_is_complete &&
        domain.hierarchy_is_topological &&
        domain.node_at_order_position.size() == count &&
        domain.parent_by_order_position.size() == count;

    std::vector<std::int32_t> parent_by_node(count, -1);
    std::vector<std::size_t> order_position_by_node(count, count);
    if (hierarchy_mapping_valid) {
        for (std::size_t order_position = 0U;
             order_position < count;
             ++order_position) {
            const auto node = static_cast<std::size_t>(
                domain.node_at_order_position[order_position]);
            if (node >= count || order_position_by_node[node] != count) {
                hierarchy_mapping_valid = false;
                break;
            }
            const auto parent = domain.parent_by_order_position[order_position];
            if (parent >= 0 && static_cast<std::size_t>(parent) >= count) {
                hierarchy_mapping_valid = false;
                break;
            }
            parent_by_node[node] =
                parent >= 0 ? static_cast<std::int32_t>(parent) : -1;
            order_position_by_node[node] = order_position;
        }
    }
    if (!hierarchy_mapping_valid) {
        std::fill(parent_by_node.begin(), parent_by_node.end(), -1);
        std::fill(order_position_by_node.begin(), order_position_by_node.end(), count);
    }

    const bool spatial_gate = CanonicalWorld::supports_spatial_hierarchy(domain);
    const auto world_matrices = spatial_gate
        ? CanonicalWorld::build_model_space_world_matrices(domain)
        : std::nullopt;
    const bool spatial_authorized =
        spatial_gate && world_matrices.has_value() &&
        world_matrices->size() == count &&
        domain.local_transform_records_by_node_index.size() == count;

    InspectionNode hierarchy;
    hierarchy.id = "transform-domain";
    hierarchy.title = "Skeleton / hierarchy";
    hierarchy.kind = InspectionKind::Collection;
    hierarchy.source_span = SourceSpan{domain.document_offset, 0U};
    hierarchy.properties.push_back({
        "NodeCount", std::to_string(count),
        EvidenceLevel::StructuralConfirmed,
    });
    hierarchy.properties.push_back({
        "PermutationComplete",
        domain.permutation_is_complete ? "true" : "false",
        EvidenceLevel::ExeConfirmed,
    });
    hierarchy.properties.push_back({
        "HierarchyTopological",
        domain.hierarchy_is_topological ? "true" : "false",
        EvidenceLevel::ExeConfirmed,
    });
    hierarchy.properties.push_back({
        "LocalTransformsComplete",
        domain.transform_records_complete ? "true" : "false",
        EvidenceLevel::ExeAndCorpusConfirmed,
    });
    hierarchy.properties.push_back({
        "LocalTransformsFinite",
        domain.transform_records_finite ? "true" : "false",
        EvidenceLevel::DataConfirmed,
    });
    hierarchy.properties.push_back({
        "SpatialHierarchy",
        spatial_authorized ? "true" : "false",
        EvidenceLevel::DataConfirmed,
    });
    hierarchy.properties.push_back({
        "WorldComposition",
        spatial_authorized
            ? "model-space: world[root]=local[root], world[node]=local[node]*world[parent]"
            : "unavailable for this document",
        EvidenceLevel::ExeConfirmed,
    });
    hierarchy.properties.push_back({
        "AdapterDomain+0x08", "preserved / semantics unresolved",
        EvidenceLevel::PreservedUndecoded,
    });

    scene->nodes.reserve(scene->nodes.size() + count);
    for (std::size_t index = 0U; index < count; ++index) {
        RenderNode render_node;
        render_node.name = "Bone " + std::to_string(index);
        render_node.kind = RenderNodeKind::Bone;
        render_node.parent = hierarchy_mapping_valid ? parent_by_node[index] : -1;
        render_node.spatial_authority = spatial_authorized;

        if (spatial_authorized) {
            const auto canonical_local = CanonicalWorld::build_local_matrix(
                domain.local_transform_records_by_node_index[index]);
            render_node.local = to_render_matrix(canonical_local);
            render_node.world = to_render_matrix((*world_matrices)[index]);
        }
        scene->nodes.push_back(std::move(render_node));

        InspectionNode bone;
        bone.id = "bone-" + std::to_string(index);
        bone.title = "Bone " + std::to_string(index);
        bone.kind = InspectionKind::Bone;
        bone.properties.push_back({
            "Parent",
            std::to_string(scene->nodes.back().parent),
            EvidenceLevel::ExeConfirmed,
        });
        if (hierarchy_mapping_valid && order_position_by_node[index] < count) {
            bone.properties.push_back({
                "EvaluationOrder",
                std::to_string(order_position_by_node[index]),
                EvidenceLevel::ExeConfirmed,
            });
        }
        if (spatial_authorized) {
            const auto& local =
                domain.local_transform_records_by_node_index[index];
            const auto position =
                CanonicalWorld::world_position((*world_matrices)[index]);
            bone.properties.push_back({
                "LocalTranslation",
                vec3_text(local.translation.x,
                          local.translation.y,
                          local.translation.z),
                EvidenceLevel::ExeAndCorpusConfirmed,
            });
            bone.properties.push_back({
                "LocalRotationXYZRadians",
                vec3_text(local.rotation_xyz_radians.x,
                          local.rotation_xyz_radians.y,
                          local.rotation_xyz_radians.z),
                EvidenceLevel::ExeAndCorpusConfirmed,
            });
            bone.properties.push_back({
                "ModelSpacePosition",
                vec3_text(position.x, position.y, position.z),
                EvidenceLevel::ExeAndCorpusConfirmed,
            });
        } else {
            bone.properties.push_back({
                "ModelSpacePosition", "unavailable",
                EvidenceLevel::Unknown,
            });
        }
        hierarchy.children.push_back(std::move(bone));
    }
    root->children.push_back(std::move(hierarchy));
    return spatial_authorized;
}

void project_skin(const CanonicalMesh& source,
                  std::uint32_t primitive_index,
                  RenderScene* scene,
                  InspectionNode* mesh_node) {
    if (scene == nullptr || mesh_node == nullptr || source.skin.empty()) return;

    SkinBinding binding;
    binding.mesh_primitive = primitive_index;
    binding.vertices.resize(source.skin.size());

    std::size_t decoded_vertices = 0U;
    std::size_t total_influences = 0U;
    for (std::size_t vertex = 0U; vertex < source.skin.size(); ++vertex) {
        const auto& decoded = source.skin[vertex];
        if (!decoded.ok()) continue;
        ++decoded_vertices;
        auto& target = binding.vertices[vertex].influences;
        target.reserve(decoded.skin.influence_count);
        for (std::size_t influence = 0U;
             influence < decoded.skin.influence_count;
             ++influence) {
            const auto& source_influence = decoded.skin.influences[influence];
            target.push_back({source_influence.bone_index, source_influence.weight});
            ++total_influences;
        }
    }
    scene->skins.push_back(std::move(binding));

    InspectionNode skin;
    skin.id = mesh_node->id + "-skin";
    skin.title = "Skin weights";
    skin.kind = InspectionKind::Skin;
    skin.properties.push_back({"Vertices", std::to_string(source.skin.size()),
                               EvidenceLevel::StructuralConfirmed});
    skin.properties.push_back({"DecodedVertices", std::to_string(decoded_vertices),
                               EvidenceLevel::DataConfirmed});
    skin.properties.push_back({"Influences", std::to_string(total_influences),
                               EvidenceLevel::DataConfirmed});
    skin.properties.push_back({"MaxInfluencesPerVertex", "3",
                               EvidenceLevel::ExeAndCorpusConfirmed});
    skin.properties.push_back({"DecodeFailures",
                               std::to_string(source.skin_decode_failures),
                               EvidenceLevel::StructuralConfirmed});
    mesh_node->children.push_back(std::move(skin));
}

void project_material_state(const CanonicalMesh& source,
                            std::uint32_t primitive_index,
                            RenderScene* scene,
                            InspectionNode* mesh_node) {
    if (scene == nullptr || mesh_node == nullptr) return;

    scene->textures.push_back(TextureBinding{
        primitive_index,
        source.texture_slot,
        "MOD runtime texture slot; bitmap source unresolved",
    });

    mesh_node->properties.push_back({
        "TextureSlot", std::to_string(source.texture_slot),
        EvidenceLevel::ExeConfirmed,
    });

    InspectionNode texture;
    texture.id = mesh_node->id + "-texture";
    texture.title = "Texture binding";
    texture.kind = InspectionKind::Texture;
    texture.properties.push_back({
        "Slot", std::to_string(source.texture_slot),
        EvidenceLevel::ExeConfirmed,
    });
    texture.properties.push_back({
        "BitmapSource", "unresolved",
        EvidenceLevel::Unknown,
    });
    mesh_node->children.push_back(std::move(texture));

    InspectionNode sampler;
    sampler.id = mesh_node->id + "-gs-clamp";
    sampler.title = "Legacy GS CLAMP REGION_REPEAT";
    sampler.kind = InspectionKind::MaterialState;
    sampler.properties.push_back({
        "MinU", std::to_string(source.gs_clamp_region_repeat.min_u),
        EvidenceLevel::ExeConfirmed,
    });
    sampler.properties.push_back({
        "MaxU", std::to_string(source.gs_clamp_region_repeat.max_u),
        EvidenceLevel::ExeConfirmed,
    });
    sampler.properties.push_back({
        "MinV", std::to_string(source.gs_clamp_region_repeat.min_v),
        EvidenceLevel::ExeConfirmed,
    });
    sampler.properties.push_back({
        "MaxV", std::to_string(source.gs_clamp_region_repeat.max_v),
        EvidenceLevel::ExeConfirmed,
    });
    mesh_node->children.push_back(std::move(sampler));
}

}  // namespace

PipelineResult run_mod_adapter(const ProbeResult& probe,
                               const std::uint8_t* bytes,
                               std::size_t size,
                               const char* module_id) noexcept {
    if (size > kMaxResourceBytes) {
        return module_support::reject(probe, module_id,
                                      "MOD rejected: resource exceeds 512 MiB reader cap");
    }
    if (size != 0U && bytes == nullptr) {
        return module_support::reject(probe, module_id,
                                      "MOD rejected: null input span");
    }

    try {
        const auto byte_span = std::span<const std::byte>{
            reinterpret_cast<const std::byte*>(bytes), size};
        const auto parsed = dmc::rengine::formats::mod::Parser::parse(byte_span);
        if (!parsed.ok()) {
            return module_support::reject(
                probe, module_id,
                "MOD rejected by canonical parser: " + first_error(parsed));
        }

        PipelineResult out;
        out.accepted = true;
        out.probe = probe;
        out.modules.push_back({"identity-probe", true});
        out.modules.push_back({"bounded-read-guard", true});
        out.modules.push_back({"canonical.mod.structural-parser", true});
        out.modules.push_back({"canonical.mod.texture-state", true});
        out.modules.push_back({module_id, true});

        out.inspection.format = "MOD";
        out.inspection.root.id = "mod";
        out.inspection.root.title = "MOD";
        out.inspection.root.kind = InspectionKind::Document;
        out.inspection.root.source_span = SourceSpan{0U, size};
        out.inspection.root.properties.push_back({
            "Version", std::to_string(parsed.document.header.version),
            EvidenceLevel::StructuralConfirmed,
        });
        out.inspection.root.properties.push_back({
            "ModelCount", std::to_string(parsed.document.outer_models.size()),
            EvidenceLevel::StructuralConfirmed,
        });

        InspectionNode models;
        models.id = "models";
        models.title = "Models / objects";
        models.kind = InspectionKind::Collection;

        std::size_t total_meshes = 0U;
        std::size_t total_vertices = 0U;
        std::size_t total_triangles = 0U;
        std::size_t total_skin_failures = 0U;

        for (std::size_t object_index = 0U;
             object_index < parsed.document.outer_models.size();
             ++object_index) {
            const auto& object = parsed.document.outer_models[object_index];
            InspectionNode object_node;
            object_node.id = "object-" + std::to_string(object_index);
            object_node.title = "Object " + std::to_string(object_index);
            object_node.kind = InspectionKind::Object;
            object_node.source_span = SourceSpan{object.record_offset, 0x40U};
            object_node.properties.push_back({
                "MeshCount", std::to_string(object.meshes.size()),
                EvidenceLevel::StructuralConfirmed,
            });
            object_node.properties.push_back({
                "AggregateElements", std::to_string(object.aggregate_element_count),
                EvidenceLevel::StructuralConfirmed,
            });

            for (std::size_t mesh_index = 0U;
                 mesh_index < object.meshes.size();
                 ++mesh_index) {
                const auto& source = object.meshes[mesh_index];
                ++total_meshes;
                total_vertices += source.positions.size();
                total_skin_failures += source.skin_decode_failures;

                MeshPrimitive primitive;
                primitive.name = "Object " + std::to_string(object_index) +
                                 " / Mesh " + std::to_string(mesh_index);
                primitive.object_index = static_cast<std::uint32_t>(object_index);
                primitive.mesh_index = static_cast<std::uint32_t>(mesh_index);
                if (!append_legacy_compatible_topology(source, &primitive.mesh)) {
                    return module_support::reject(
                        probe, module_id,
                        "MOD canonical projection rejected mesh topology/size limits");
                }
                total_triangles += primitive.mesh.indices.size() / 3U;

                const auto primitive_index =
                    static_cast<std::uint32_t>(out.scene.meshes.size());

                InspectionNode mesh_node;
                mesh_node.id = "object-" + std::to_string(object_index) +
                               "-mesh-" + std::to_string(mesh_index);
                mesh_node.title = "Mesh " + std::to_string(mesh_index);
                mesh_node.kind = InspectionKind::Mesh;
                mesh_node.source_span = SourceSpan{source.record_offset, 0x50U};
                mesh_node.properties.push_back({
                    "Vertices", std::to_string(source.positions.size()),
                    EvidenceLevel::StructuralConfirmed,
                });
                mesh_node.properties.push_back({
                    "Triangles", std::to_string(primitive.mesh.indices.size() / 3U),
                    EvidenceLevel::DataConfirmed,
                });
                mesh_node.properties.push_back({
                    "Normals", std::to_string(source.normals.size()),
                    EvidenceLevel::StructuralConfirmed,
                });
                mesh_node.properties.push_back({
                    "UVs", std::to_string(source.uvs.size()),
                    EvidenceLevel::StructuralConfirmed,
                });
                mesh_node.properties.push_back({
                    "TopologyBreakWords", std::to_string(source.control_words.size()),
                    EvidenceLevel::StructuralConfirmed,
                });

                project_material_state(source, primitive_index, &out.scene, &mesh_node);
                project_skin(source, primitive_index, &out.scene, &mesh_node);
                object_node.children.push_back(std::move(mesh_node));
                out.scene.meshes.push_back(std::move(primitive));
            }
            models.children.push_back(std::move(object_node));
        }

        out.inspection.root.children.push_back(std::move(models));
        const bool spatial_hierarchy =
            project_hierarchy(parsed.document, &out.scene, &out.inspection.root);
        if (spatial_hierarchy) {
            out.modules.push_back({"canonical.mod.spatial-hierarchy", true});
        }

        if (!parsed.diagnostics.empty()) {
            InspectionNode diagnostics;
            diagnostics.id = "diagnostics";
            diagnostics.title = "Diagnostics";
            diagnostics.kind = InspectionKind::Collection;
            diagnostics.children.reserve(parsed.diagnostics.size());
            for (std::size_t i = 0U; i < parsed.diagnostics.size(); ++i) {
                diagnostics.children.push_back(make_diagnostic_node(parsed.diagnostics[i], i));
            }
            out.inspection.root.children.push_back(std::move(diagnostics));
        }

        out.renderable = out.scene.has_geometry();
        std::ostringstream detail;
        detail << "MOD canonical C++20 reader"
               << " | objects=" << parsed.document.outer_models.size()
               << " meshes=" << total_meshes
               << " vertices=" << total_vertices
               << " triangles=" << total_triangles
               << " nodes=" << out.scene.nodes.size()
               << " textures=" << out.scene.textures.size()
               << " spatialHierarchy=" << (spatial_hierarchy ? "yes" : "no")
               << " skinFailures=" << total_skin_failures
               << " diagnostics=" << parsed.diagnostics.size();
        out.detail = detail.str();
        return out;
    } catch (const std::bad_alloc&) {
        return module_support::reject(probe, module_id,
                                      "MOD rejected: allocation failed");
    } catch (...) {
        return module_support::reject(probe, module_id,
                                      "MOD rejected: unexpected canonical adapter failure");
    }
}

}  // namespace dmcresource::adapters