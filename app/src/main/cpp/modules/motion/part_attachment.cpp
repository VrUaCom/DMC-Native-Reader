#include "dmcresource/motion/part_attachment.h"

#include <array>
#include <cctype>
#include <cmath>
#include <new>
#include <optional>
#include <vector>

#include "dmc_rengine/analysis/mod/animation_binding.hpp"
#include "dmc_rengine/formats/mod/world_transform.hpp"
#include "dmcresource/motion/skeleton_rig.h"
#include "dmcresource/resource_session.h"
#include "dmcresource/scene_projection.h"

namespace dmcresource::motion {
namespace {

namespace world = dmc::rengine::formats::mod::world_transform;
namespace animation = dmc::rengine::analysis::mod;

struct Range final {
    std::size_t begin{};
    std::size_t count{};
};

[[nodiscard]] std::size_t vertex_count(const RenderScene& scene) noexcept {
    std::size_t total = 0U;
    for (const auto& primitive : scene.meshes) total += primitive.mesh.vertices.size();
    return total;
}

[[nodiscard]] std::optional<Range> vertex_range(const Session& session, std::size_t part) {
    std::size_t cursor = 0U;
    Range out;
    for (std::size_t index = 0U; index < session.composite_parts.size(); ++index) {
        const auto count = vertex_count(session.composite_parts[index].scene);
        if (index == part) out = {cursor, count};
        cursor += count;
    }
    if (cursor != session.render_mesh.vertices.size()) return std::nullopt;
    return out;
}

[[nodiscard]] std::optional<Range> node_range(const Session& session, std::size_t part) {
    std::size_t cursor = 0U;
    Range out;
    for (std::size_t index = 0U; index < session.composite_parts.size(); ++index) {
        const auto count = session.composite_parts[index].scene.nodes.size();
        if (index == part) out = {cursor, count};
        cursor += count;
    }
    if (cursor != session.scene.nodes.size()) return std::nullopt;
    return out;
}

[[nodiscard]] Vec3 transform_row(const Vec3& p, const std::array<float, 16>& m) noexcept {
    return {p.x * m[0] + p.y * m[4] + p.z * m[8] + m[12],
            p.x * m[1] + p.y * m[5] + p.z * m[9] + m[13],
            p.x * m[2] + p.y * m[6] + p.z * m[10] + m[14]};
}

[[nodiscard]] bool finite(const std::array<float, 16>& m) noexcept {
    for (const float value : m) {
        if (!std::isfinite(value)) return false;
    }
    return true;
}

[[nodiscard]] bool pose_part(Session* session, std::size_t part_index) {
    auto& part = session->composite_parts[part_index];
    const auto& placement = part.placement;
    if (placement.mode != CompositePlacementMode::HostJointSkeleton ||
        placement.host_part_index >= session->composite_parts.size()) {
        return false;
    }
    const bool rigged = part.scene.rig != nullptr &&
        part.scene.rig->node_count() == part.scene.nodes.size();
    const auto host_nodes = node_range(*session, placement.host_part_index);
    const auto child_nodes = node_range(*session, part_index);
    const auto child_vertices = vertex_range(*session, part_index);
    if (!host_nodes || !child_nodes || !child_vertices ||
        placement.attachment_selector >= host_nodes->count) {
        return false;
    }

    world::Matrix4f host_world{};
    host_world.values =
        session->scene.nodes[host_nodes->begin + placement.attachment_selector].world.values;
    world::Matrix4f offset{};
    offset.values = placement.attachment_offset.values;
    // 0x140030E40(dest, jointWorld, offset): dest = offset x jointWorld.
    const auto root_base = world::multiply_dmc3_matrices(offset, host_world);
    if (!finite(root_base.values)) return false;

    if (!rigged) {
        // No spatial skeleton: move the whole part rigidly onto the root base.
        Mesh rest;
        if (!materialize_render_scene(part.scene, &rest) ||
            rest.vertices.size() != child_vertices->count) {
            return false;
        }
        for (std::size_t i = 0U; i < rest.vertices.size(); ++i) {
            const auto moved = transform_row(rest.vertices[i], root_base.values);
            if (!std::isfinite(moved.x) || !std::isfinite(moved.y) || !std::isfinite(moved.z)) {
                return false;
            }
            session->render_mesh.vertices[child_vertices->begin + i] = moved;
        }
        return true;
    }

    const auto& domain = part.scene.rig->domain;
    const auto count = part.scene.rig->node_count();
    std::vector<world::Matrix4f> locals(count);
    for (std::size_t node = 0U; node < count; ++node) {
        locals[node] = world::build_local_matrix(
            domain.local_transform_records_by_node_index[node]);
    }
    if (placement.root_local_identity && !domain.node_at_order_position.empty()) {
        const auto root = static_cast<std::size_t>(domain.node_at_order_position.front());
        if (root < count) locals[root] = world::identity_matrix();
    }

    std::optional<std::vector<world::Matrix4f>> current;
    if (placement.node_constraints.empty()) {
        current = animation::build_animated_world_matrices(domain, locals, root_base);
    } else {
        // 0x14030E680: a joint with an enabled constraint takes its world from
        // the constraint (mode 1: offset x host world, 0x1402CBBE0); the other
        // joints compose local x parent (0x14030E9B0).
        const auto binding = animation::project_animation_binding(domain);
        if (!binding || binding->by_node_index.size() != count) return false;
        current.emplace(count);
        for (std::size_t position = 0U; position < count; ++position) {
            const auto node = static_cast<std::size_t>(binding->node_at_order_position[position]);
            if (node >= count) return false;
            const CompositeNodeConstraint* constraint = nullptr;
            for (const auto& candidate : placement.node_constraints) {
                if (candidate.child_node == node) constraint = &candidate;
            }
            if (constraint != nullptr) {
                if (constraint->host_node >= host_nodes->count) return false;
                world::Matrix4f joint{};
                joint.values =
                    session->scene.nodes[host_nodes->begin + constraint->host_node].world.values;
                world::Matrix4f local_offset{};
                local_offset.values = constraint->offset.values;
                (*current)[node] = world::multiply_dmc3_matrices(local_offset, joint);
                continue;
            }
            const auto parent = binding->by_node_index[node].parent_node_index;
            if (parent < 0) {
                (*current)[node] = world::multiply_dmc3_matrices(locals[node], root_base);
            } else if (static_cast<std::size_t>(parent) < count) {
                (*current)[node] = world::multiply_dmc3_matrices(
                    locals[node], (*current)[static_cast<std::size_t>(parent)]);
            } else {
                return false;
            }
        }
    }
    const auto inverse_rest = world::build_model_space_inverse_rest_matrices(domain);
    if (!current || !inverse_rest || current->size() != count || inverse_rest->size() != count) {
        return false;
    }
    std::vector<world::Matrix4f> palette(count);
    for (std::size_t node = 0U; node < count; ++node) {
        palette[node] = world::multiply_dmc3_matrices((*inverse_rest)[node], (*current)[node]);
    }
    const auto root = domain.node_at_order_position.empty()
        ? std::size_t{0}
        : static_cast<std::size_t>(domain.node_at_order_position.front());

    Mesh rest;
    if (!materialize_render_scene(part.scene, &rest) ||
        rest.vertices.size() != child_vertices->count) {
        return false;
    }

    std::size_t vertex = 0U;
    auto& out = session->render_mesh.vertices;
    for (std::size_t primitive = 0U; primitive < part.scene.meshes.size(); ++primitive) {
        const auto& mesh = part.scene.meshes[primitive].mesh;
        const SkinBinding* binding = nullptr;
        for (const auto& candidate : part.scene.skins) {
            if (candidate.mesh_primitive == primitive &&
                candidate.vertices.size() == mesh.vertices.size()) {
                binding = &candidate;
                break;
            }
        }
        for (std::size_t local = 0U; local < mesh.vertices.size(); ++local, ++vertex) {
            const auto& source = rest.vertices[vertex];
            Vec3 skinned{};
            float total = 0.0F;
            if (binding != nullptr) {
                for (const auto& joint : binding->vertices[local].influences) {
                    if (joint.node_index >= count || !(joint.weight > 0.0F)) continue;
                    const auto moved = transform_row(source, palette[joint.node_index].values);
                    skinned.x += moved.x * joint.weight;
                    skinned.y += moved.y * joint.weight;
                    skinned.z += moved.z * joint.weight;
                    total += joint.weight;
                }
            }
            if (total > 0.0F) {
                skinned = {skinned.x / total, skinned.y / total, skinned.z / total};
            } else {
                // No influence: follow the part root rigidly.
                skinned = transform_row(source, palette[root < count ? root : 0U].values);
            }
            if (!std::isfinite(skinned.x) || !std::isfinite(skinned.y) ||
                !std::isfinite(skinned.z)) {
                return false;
            }
            out[child_vertices->begin + vertex] = skinned;
        }
    }
    for (std::size_t node = 0U; node < count; ++node) {
        session->scene.nodes[child_nodes->begin + node].world.values = (*current)[node].values;
    }
    return true;
}

}  // namespace

bool attach_part_skeleton(Session* session,
                          std::size_t host_part,
                          std::size_t child_part,
                          std::uint32_t host_joint,
                          bool root_local_identity,
                          const Matrix4& offset) noexcept {
    if (session == nullptr || host_part == child_part ||
        host_part >= session->composite_parts.size() ||
        child_part >= session->composite_parts.size()) {
        return false;
    }
    try {
        auto& part = session->composite_parts[child_part];
        const auto previous = part.placement;
        part.placement.mode = CompositePlacementMode::HostJointSkeleton;
        part.placement.host_part_index = host_part;
        part.placement.host_instance_id = session->composite_parts[host_part].instance_id;
        part.placement.attachment_selector = host_joint;
        part.placement.root_local_identity = root_local_identity;
        part.placement.attachment_offset = offset;
        part.placement.resolved = true;
        if (!pose_part(session, child_part)) {
            part.placement = previous;
            return false;
        }
        HierarchyOverlay overlay;
        if (materialize_hierarchy_overlay(session->scene, &overlay)) {
            session->hierarchy_overlay = std::move(overlay);
        }
        return true;
    } catch (...) {
        return false;
    }
}

bool attach_part_nodes(Session* session,
                       std::size_t host_part,
                       std::size_t child_part,
                       std::span<const CompositeNodeConstraint> constraints) noexcept {
    if (session == nullptr || constraints.empty() || host_part == child_part ||
        host_part >= session->composite_parts.size() ||
        child_part >= session->composite_parts.size()) {
        return false;
    }
    try {
        auto& part = session->composite_parts[child_part];
        if (part.scene.rig == nullptr || part.scene.rig->node_count() != part.scene.nodes.size()) {
            return false;
        }
        const auto previous = part.placement;
        part.placement.mode = CompositePlacementMode::HostJointSkeleton;
        part.placement.host_part_index = host_part;
        part.placement.host_instance_id = session->composite_parts[host_part].instance_id;
        part.placement.attachment_selector = 0U;  // model root follows the body root
        part.placement.root_local_identity = false;
        part.placement.attachment_offset = Matrix4{};
        part.placement.node_constraints.assign(constraints.begin(), constraints.end());
        part.placement.resolved = true;
        if (!pose_part(session, child_part)) {
            part.placement = previous;
            return false;
        }
        HierarchyOverlay overlay;
        if (materialize_hierarchy_overlay(session->scene, &overlay)) {
            session->hierarchy_overlay = std::move(overlay);
        }
        return true;
    } catch (...) {
        return false;
    }
}

std::optional<EnemyPartConstraints> enemy_constraints_for(std::string_view archive_name,
                                                          std::uint32_t part_slot) noexcept {
    const auto slash = archive_name.find_last_of("/\\");
    if (slash != std::string_view::npos) archive_name.remove_prefix(slash + 1U);
    for (const auto& record : kEnemyPartConstraints) {
        if (record.part_slot != part_slot) continue;
        const auto& stem = record.pac_stem;
        if (archive_name.size() != stem.size() + 4U) continue;
        bool match = true;
        for (std::size_t i = 0U; i < archive_name.size() && match; ++i) {
            const char expected = i < stem.size() ? stem[i] : ".pac"[i - stem.size()];
            match = std::tolower(static_cast<unsigned char>(archive_name[i])) == expected;
        }
        if (match) return record;
    }
    return std::nullopt;
}

bool apply_part_attachments(Session* session) noexcept {
    if (session == nullptr) return false;
    try {
        bool ok = true;
        for (std::size_t part = 0U; part < session->composite_parts.size(); ++part) {
            if (session->composite_parts[part].placement.mode ==
                CompositePlacementMode::HostJointSkeleton) {
                ok = pose_part(session, part) && ok;
            }
        }
        return ok;
    } catch (...) {
        return false;
    }
}

std::optional<WeaponAttachRecord> weapon_record_for_archive(
    std::string_view archive_name) noexcept {
    const auto slash = archive_name.find_last_of("/\\");
    if (slash != std::string_view::npos) archive_name.remove_prefix(slash + 1U);
    for (const auto& record : kWeaponState0Records) {
        const auto& stem = record.pac_stem;
        if (archive_name.size() != stem.size() + 4U) continue;
        bool match = true;
        for (std::size_t i = 0U; i < archive_name.size() && match; ++i) {
            const char expected = i < stem.size() ? stem[i] : ".pac"[i - stem.size()];
            match = std::tolower(static_cast<unsigned char>(archive_name[i])) == expected;
        }
        if (match) return record;
    }
    return std::nullopt;
}

Matrix4 attach_local_matrix(const std::array<float, 3>& translation,
                            const std::array<float, 3>& rotation_xyz_radians) noexcept {
    dmc::rengine::formats::mod::transform_domain::LocalTransformRecord local{};
    local.translation = {translation[0], translation[1], translation[2]};
    local.rotation_xyz_radians = {rotation_xyz_radians[0], rotation_xyz_radians[1],
                                  rotation_xyz_radians[2]};
    const auto built = world::build_local_matrix(local);
    Matrix4 out;
    out.values = built.values;
    out.values[12] = translation[0];
    out.values[13] = translation[1];
    out.values[14] = translation[2];
    out.values[15] = 1.0F;
    return out;
}

Matrix4 weapon_offset_matrix(const WeaponAttachRecord& record) noexcept {
    return attach_local_matrix(record.translation, record.rotation_xyz_radians);
}

std::optional<WeaponSecondPart> weapon_second_part(std::string_view class_name) noexcept {
    for (const auto& part : kWeaponSecondParts) {
        if (part.class_name == class_name) return part;
    }
    return std::nullopt;
}

bool is_attached_part(const Session* session, std::size_t part) noexcept {
    return session != nullptr && part < session->composite_parts.size() &&
           session->composite_parts[part].placement.mode ==
               CompositePlacementMode::HostJointSkeleton;
}

}  // namespace dmcresource::motion
