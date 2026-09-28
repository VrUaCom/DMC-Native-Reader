#include "dmcresource/motion/part_attachment.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cctype>
#include <cmath>
#include <memory>
#include <new>
#include <optional>
#include <vector>

#include "dmc_rengine/analysis/mod/animation_binding.hpp"
#include "dmc_rengine/formats/mod/world_transform.hpp"
#include "dmcresource/motion/cloth_chain.h"
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

[[nodiscard]] bool pose_part(Session* session, std::size_t part_index, std::uint32_t cloth_steps = 0U) {
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

    ClothState* cloth = placement.cloth.get();
    if (cloth != nullptr &&
        (cloth->sim.size() != count || cloth->velocity.size() != count ||
         cloth->axis_by_node.size() != count)) {
        cloth = nullptr;
    }
    std::optional<std::vector<world::Matrix4f>> current;
    if (placement.node_constraints.empty() && cloth == nullptr) {
        current = animation::build_animated_world_matrices(domain, locals, root_base);
    } else {
        // 0x14030E680: a joint with an enabled constraint takes its world from
        // the constraint (mode 1: offset x host world, 0x1402CBBE0; chain
        // nodes: 0x1402C9450); the other joints compose local x parent
        // (0x14030E9B0).
        const auto binding = animation::project_animation_binding(domain);
        if (!binding || binding->by_node_index.size() != count) return false;
        current.emplace(count, root_base);
        // Collision capsules ride the host joints' current worlds.
        std::vector<WorldCapsule> capsules;
        if (cloth != nullptr) {
            for (const auto& capsule : cloth->capsules) {
                if (capsule.host_joint >= host_nodes->count) continue;
                const auto& m =
                    session->scene.nodes[host_nodes->begin + capsule.host_joint].world.values;
                const auto place = [&m](const std::array<float, 3>& p) {
                    return std::array<float, 3>{
                        p[0] * m[0] + p[1] * m[4] + p[2] * m[8] + m[12],
                        p[0] * m[1] + p[1] * m[5] + p[2] * m[9] + m[13],
                        p[0] * m[2] + p[1] * m[6] + p[2] * m[10] + m[14]};
                };
                capsules.push_back({place(capsule.a), place(capsule.b), capsule.radius});
            }
        }
        const bool step = cloth != nullptr && (cloth_steps > 0U || !cloth->initialized);
        const std::uint32_t passes = step ? std::max<std::uint32_t>(cloth_steps, 1U) : 1U;
        for (std::uint32_t pass = 0U; pass < passes; ++pass) {
            for (std::size_t position = 0U; position < count; ++position) {
                const auto node =
                    static_cast<std::size_t>(binding->node_at_order_position[position]);
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
                const world::Matrix4f* parent_world = nullptr;
                if (parent < 0) {
                    parent_world = &root_base;
                } else if (static_cast<std::size_t>(parent) < count) {
                    parent_world = &(*current)[static_cast<std::size_t>(parent)];
                } else {
                    return false;
                }
                const auto target = world::multiply_dmc3_matrices(locals[node], *parent_world);
                if (cloth == nullptr || cloth->axis_by_node[node] < 0) {
                    (*current)[node] = target;
                    continue;
                }
                if (!cloth->initialized) {
                    cloth->sim[node] = target.values;
                    cloth->velocity[node] = {};
                }
                if (!step) {
                    (*current)[node].values = cloth->sim[node];
                    continue;
                }
                const auto wind_parent = static_cast<std::size_t>(
                    std::max(cloth->params_for(static_cast<std::uint32_t>(node)).wind_parent, 0));
                const auto& wind_world =
                    wind_parent < count ? (*current)[wind_parent] : root_base;
                const auto& t = locals[node].values;
                const float rest = std::sqrt(t[12] * t[12] + t[13] * t[13] + t[14] * t[14]);
                (*current)[node].values = step_cloth_node(
                    *cloth, static_cast<std::uint32_t>(node), target.values,
                    parent_world->values, wind_world.values, rest, 1.0F, capsules);
                if (!finite((*current)[node].values)) {
                    // Diverged: restart this node from its rest target.
                    cloth->sim[node] = target.values;
                    cloth->velocity[node] = {};
                    (*current)[node] = target;
                }
            }
            if (cloth != nullptr) cloth->initialized = true;
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

std::vector<CompositeNodeConstraint> parse_coat_constraints(std::span<const std::uint8_t> bytes) {
    std::vector<CompositeNodeConstraint> out;
    const auto u32 = [&bytes](std::size_t o) {
        return static_cast<std::uint32_t>(bytes[o]) |
               (static_cast<std::uint32_t>(bytes[o + 1U]) << 8U) |
               (static_cast<std::uint32_t>(bytes[o + 2U]) << 16U) |
               (static_cast<std::uint32_t>(bytes[o + 3U]) << 24U);
    };
    if (bytes.size() < 0x10U || bytes[0] != 'C' || bytes[1] != 'C' || bytes[2] != 'N' ||
        bytes[3] != 'S' || u32(4U) != 1U || u32(8U) > 16U) {
        return out;
    }
    const std::size_t count = u32(8U);
    for (std::size_t i = 0U; i < count; ++i) {
        const std::size_t o = 0x10U + i * 0x50U;
        if (o + 0x50U > bytes.size()) break;
        const auto node = u32(o);
        const auto joint = u32(o + 4U);
        if (node >= kPlayerCoatJointCapacity || joint >= 96U) continue;
        CompositeNodeConstraint c;
        c.child_node = node;
        c.host_node = joint;
        for (std::size_t k = 0U; k < 16U; ++k) {
            c.offset.values[k] = std::bit_cast<float>(u32(o + 0x10U + k * 4U));
        }
        out.push_back(c);
    }
    return out;
}

std::array<ClothCapsule, 6> player_coat_capsules(std::span<const std::uint8_t> bytes) {
    std::array<ClothCapsule, 6> out{};
    std::copy(kPlayerCoatCapsules.begin(), kPlayerCoatCapsules.end(), out.begin());
    const auto u32 = [&bytes](std::size_t o) {
        return static_cast<std::uint32_t>(bytes[o]) |
               (static_cast<std::uint32_t>(bytes[o + 1U]) << 8U) |
               (static_cast<std::uint32_t>(bytes[o + 2U]) << 16U) |
               (static_cast<std::uint32_t>(bytes[o + 3U]) << 24U);
    };
    const auto f32 = [&](std::size_t o) { return std::bit_cast<float>(u32(o)); };
    if (bytes.size() < 0x10U || bytes[0] != 'C' || bytes[1] != 'C' || bytes[2] != 'N' ||
        bytes[3] != 'S' || u32(4U) != 1U || u32(8U) > 16U || u32(12U) > 6U) {
        return out;
    }
    std::size_t o = 0x10U + static_cast<std::size_t>(u32(8U)) * 0x50U;
    for (std::uint32_t i = 0U; i < u32(12U) && o + 0x40U <= bytes.size(); ++i, o += 0x40U) {
        const auto index = u32(o);
        if (index >= out.size()) continue;
        out[index].a = {f32(o + 0x10U), f32(o + 0x14U), f32(o + 0x18U)};
        out[index].b = {f32(o + 0x20U), f32(o + 0x24U), f32(o + 0x28U)};
        out[index].radius = f32(o + 0x30U);
    }
    return out;
}

bool set_part_node_constraints(Session* session,
                               std::size_t part_index,
                               std::span<const CompositeNodeConstraint> constraints) noexcept {
    if (session == nullptr || part_index >= session->composite_parts.size()) return false;
    try {
        auto& part = session->composite_parts[part_index];
        if (!part.placement.resolved) return false;
        for (const auto& c : constraints) {
            if (c.child_node >= part.scene.nodes.size()) return false;
        }
        const auto previous = part.placement.node_constraints;
        part.placement.node_constraints.assign(constraints.begin(), constraints.end());
        if (!pose_part(session, part_index)) {
            part.placement.node_constraints = previous;
            return false;
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

std::optional<std::uint32_t> tsc_slot_for(std::string_view archive_name,
                                          std::uint32_t model_slot) noexcept {
    const auto slash = archive_name.find_last_of("/\\");
    if (slash != std::string_view::npos) archive_name.remove_prefix(slash + 1U);
    for (const auto& record : kTscSources) {
        bool used = false;
        for (std::uint32_t k = 0U; k < record.model_count; ++k) {
            used = used || record.model_slots[k] == model_slot;
        }
        if (!used) continue;
        const auto& stem = record.pac_stem;
        if (archive_name.size() != stem.size() + 4U) continue;
        bool match = true;
        for (std::size_t i = 0U; i < archive_name.size() && match; ++i) {
            const char expected = i < stem.size() ? stem[i] : ".pac"[i - stem.size()];
            match = std::tolower(static_cast<unsigned char>(archive_name[i])) == expected;
        }
        if (match) return record.tsc_slot;
    }
    return std::nullopt;
}

std::optional<std::uint32_t> enemy_cloth_slot(std::string_view archive_name,
                                              std::uint32_t model_slot) noexcept {
    const auto slash = archive_name.find_last_of("/\\");
    if (slash != std::string_view::npos) archive_name.remove_prefix(slash + 1U);
    for (const auto& record : kEnemyClothSources) {
        if (record.model_slot != model_slot) continue;
        const auto& stem = record.pac_stem;
        if (archive_name.size() != stem.size() + 4U) continue;
        bool match = true;
        for (std::size_t i = 0U; i < archive_name.size() && match; ++i) {
            const char expected = i < stem.size() ? stem[i] : ".pac"[i - stem.size()];
            match = std::tolower(static_cast<unsigned char>(archive_name[i])) == expected;
        }
        if (match) return record.clt_slot;
    }
    return std::nullopt;
}

bool apply_part_attachments(Session* session) noexcept {
    return apply_part_attachments(session, 0U);
}

bool apply_part_attachments(Session* session, std::uint32_t cloth_steps) noexcept {
    if (session == nullptr) return false;
    try {
        bool ok = true;
        for (std::size_t part = 0U; part < session->composite_parts.size(); ++part) {
            if (session->composite_parts[part].placement.mode ==
                CompositePlacementMode::HostJointSkeleton) {
                ok = pose_part(session, part, cloth_steps) && ok;
            }
        }
        return ok;
    } catch (...) {
        return false;
    }
}

void reset_part_cloth(Session* session) noexcept {
    if (session == nullptr) return;
    for (auto& part : session->composite_parts) {
        if (part.placement.cloth != nullptr) part.placement.cloth->initialized = false;
    }
}

std::size_t attach_part_cloth(Session* session,
                              std::size_t part_index,
                              std::string_view clt_text,
                              std::uint32_t settle_steps,
                              std::span<const ClothCapsule> capsules) noexcept {
    if (session == nullptr || part_index >= session->composite_parts.size()) return 0U;
    try {
        auto& part = session->composite_parts[part_index];
        if (part.placement.mode != CompositePlacementMode::HostJointSkeleton ||
            part.scene.rig == nullptr || part.scene.rig->node_count() != part.scene.nodes.size()) {
            return 0U;
        }
        auto blocks = parse_clt(clt_text);
        if (blocks.empty()) return 0U;
        const auto count = part.scene.rig->node_count();
        auto state = std::make_shared<ClothState>();
        state->params = blocks.front();
        state->blocks = std::move(blocks);
        state->sim.assign(count, {});
        state->velocity.assign(count, {});
        state->axis_by_node.assign(count, -1);
        state->block_by_node.assign(count, 0U);
        std::size_t simulated = 0U;
        // Every ClothNo block (0x1402CA1D0 reads ClothNum of them).
        for (std::size_t b = 0U; b < state->blocks.size() && b < 255U; ++b) {
            for (const auto& bone : state->blocks[b].bones) {
                if (bone.node >= count || bone.node == 0U) continue;
                if (state->axis_by_node[bone.node] < 0) ++simulated;
                state->axis_by_node[bone.node] = static_cast<std::int8_t>(bone.axis);
                state->block_by_node[bone.node] = static_cast<std::uint8_t>(b);
            }
        }
        if (simulated == 0U) return 0U;
        state->capsules.assign(capsules.begin(), capsules.end());
        const auto previous = part.placement.cloth;
        part.placement.cloth = state;
        if (!pose_part(session, part_index, std::max<std::uint32_t>(settle_steps, 1U))) {
            part.placement.cloth = previous;
            (void)pose_part(session, part_index);
            return 0U;
        }
        HierarchyOverlay overlay;
        if (materialize_hierarchy_overlay(session->scene, &overlay)) {
            session->hierarchy_overlay = std::move(overlay);
        }
        return simulated;
    } catch (...) {
        return 0U;
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

bool set_lady_component_preset(Session* session,
                               LadyComponentBinding& binding,
                               LadyPlacementPreset preset) noexcept {
    if (session == nullptr || binding.host_part >= session->composite_parts.size() ||
        binding.part >= session->composite_parts.size()) {
        return false;
    }
    const auto* contract = lady_component_contract(binding.component);
    if (contract == nullptr || contract->model_slot != binding.model_slot) return false;
    const auto index = static_cast<std::size_t>(preset);
    if (index >= contract->presets.size()) return false;
    const auto& record = contract->presets[index];

    // CEm034 component3/preset1 deliberately bypasses serialized node13 and
    // points CCnsMatrix at CEm034+0x43C0. Canonical EXE reconstruction:
    //   +0x43C0 = bodyManager(+0x850)->currentWorld(+0x110)
    //   effective parent = S(+0x4400) * bodyRoot
    // Both retail Lady bodies (slots1/32) have node0 identity at rest, so the
    // same result is materialized as (local * S) * hostRootWorld.
    if (record.effective_parent == LadyEffectiveParent::RuntimeBodyRootScaled) {
        world::Matrix4f local{};
        local.values =
            attach_local_matrix(record.translation, record.rotation_xyz_radians).values;
        auto scale = world::identity_matrix();
        scale.values[0] = binding.runtime_uniform_scale;
        scale.values[5] = binding.runtime_uniform_scale;
        scale.values[10] = binding.runtime_uniform_scale;
        const auto local_scaled = world::multiply_dmc3_matrices(local, scale);
        Matrix4 offset;
        offset.values = local_scaled.values;
        if (!attach_part_skeleton(
                session, binding.host_part, binding.part, 0U, false, offset)) {
            return false;
        }
        binding.preset = preset;
        return true;
    }

    if (!attach_part_skeleton(
            session, binding.host_part, binding.part, record.serialized_node,
            false, attach_local_matrix(record.translation,
                                       record.rotation_xyz_radians))) {
        return false;
    }
    binding.preset = preset;
    if (binding.component == 3U && preset == LadyPlacementPreset::BodyStowed) {
        binding.runtime_uniform_scale = 1.0F;
    }
    return true;
}

bool set_lady_component_control_domain(Session* session,
                                       LadyComponentBinding& binding,
                                       LadyControlDomain domain) noexcept {
    if (session == nullptr || binding.component != 0U ||
        binding.part >= session->composite_parts.size()) {
        return false;
    }
    // Placement and control are orthogonal in CEm034. Most independent-script
    // entries use BodyStowed, but states 0x7C/0x7D retain ActiveDeployed while
    // +0x4020 selects em034_013. Never collapse these into one scalar state.
    auto& part = session->composite_parts[binding.part];
    if (domain == LadyControlDomain::IndependentMotionScript) {
        // Freeze the currently resolved equipment world as the root for its
        // own 3-node MOT domain. HostJointSkeleton must be disabled, otherwise
        // the generic motion player correctly treats the part as host-driven
        // and refuses to animate it independently.
        if (!part.placement.resolved) return false;
        part.placement.mode = CompositePlacementMode::HostJoint;
        binding.control_domain = domain;
        return true;
    }

    binding.control_domain = LadyControlDomain::BodyConstraint;
    // Re-enter the EXE placement helper domain and rebuild the current preset
    // against the freshly posed Lady body.
    return set_lady_component_preset(session, binding, binding.preset);
}

bool set_lady_component_runtime_scale(Session* session,
                                      LadyComponentBinding& binding,
                                      float uniform_scale) noexcept {
    if (session == nullptr || binding.component != 3U ||
        !std::isfinite(uniform_scale) || uniform_scale < 0.0F) {
        return false;
    }
    binding.runtime_uniform_scale = uniform_scale;
    if (binding.preset != LadyPlacementPreset::ActiveDeployed) return true;
    return set_lady_component_preset(
        session, binding, LadyPlacementPreset::ActiveDeployed);
}

namespace {

[[nodiscard]] LadyComponentBinding* lady_binding(Session* session,
                                                 std::uint8_t component) noexcept {
    if (session == nullptr) return nullptr;
    for (auto& binding : session->lady_component_bindings) {
        if (binding.component == component) return &binding;
    }
    return nullptr;
}

[[nodiscard]] bool apply_lady_preset(Session* session,
                                     std::uint8_t component,
                                     LadyPlacementPreset preset,
                                     LadyRuntimeApplyResult& out) noexcept {
    auto* binding = lady_binding(session, component);
    if (binding == nullptr) return false;
    const bool materialized = set_lady_component_preset(session, *binding, preset);
    // Runtime state remains known even when the preview lacks the special
    // RuntimeBodyRootScaled materialization bridge.
    binding->preset = preset;
    ++out.changed_components;
    if (!materialized) out.fully_materialized = false;
    return true;
}

void set_lady_domain(Session* session, LadyControlDomain domain,
                     LadyRuntimeApplyResult& out) noexcept {
    if (auto* binding = lady_binding(session, 0U); binding != nullptr) {
        if (set_lady_component_control_domain(session, *binding, domain)) {
            ++out.changed_components;
        }
    }
}

[[nodiscard]] bool in_range(std::uint16_t v,
                            std::uint16_t first,
                            std::uint16_t last) noexcept {
    return v >= first && v <= last;
}

void reset_lady_components(Session* session, LadyRuntimeApplyResult& out) noexcept {
    for (std::uint8_t component = 0U; component < 5U; ++component) {
        (void)apply_lady_preset(
            session, component, LadyPlacementPreset::BodyStowed, out);
    }
    set_lady_domain(session, LadyControlDomain::BodyConstraint, out);
}

}  // namespace

LadyRuntimeApplyResult apply_lady_state_entry(Session* session,
                                              std::uint16_t state) noexcept {
    LadyRuntimeApplyResult out;
    if (session == nullptr || session->lady_component_bindings.empty()) {
        out.fully_materialized = false;
        return out;
    }

    const bool group_component1 =
        state == 0x2EU || in_range(state, 0x30U, 0x52U);
    const bool group_component0_active =
        in_range(state, 0x53U, 0x59U) || state == 0x5EU || state == 0x61U;
    const bool group_component0_independent =
        in_range(state, 0x5AU, 0x5DU) || in_range(state, 0x64U, 0x73U);

    if (group_component1) {
        out.recognized = true;
        reset_lady_components(session, out);
        (void)apply_lady_preset(
            session, 1U, LadyPlacementPreset::ActiveDeployed, out);
    } else if (group_component0_active) {
        out.recognized = true;
        reset_lady_components(session, out);
        (void)apply_lady_preset(
            session, 0U, LadyPlacementPreset::ActiveDeployed, out);
        if (state == 0x5EU) out.dynamic_actor = 3;  // immediate CEm034Shl03
    } else if (group_component0_independent) {
        out.recognized = true;
        reset_lady_components(session, out);
        set_lady_domain(session, LadyControlDomain::IndependentMotionScript, out);
    } else if (state == 0x5FU || state == 0x62U) {
        out.recognized = true;
        reset_lady_components(session, out);
        (void)apply_lady_preset(
            session, 0U, LadyPlacementPreset::ActiveDeployed, out);
    } else if (state == 0x60U || state == 0x63U) {
        out.recognized = true;
        reset_lady_components(session, out);
        (void)apply_lady_preset(
            session, 0U, LadyPlacementPreset::ActiveDeployed, out);
        (void)apply_lady_preset(
            session, 4U, LadyPlacementPreset::ActiveDeployed, out);
    } else if (state == 0x7BU) {
        out.recognized = true;
        reset_lady_components(session, out);
        set_lady_domain(session, LadyControlDomain::IndependentMotionScript, out);
    } else if (state == 0x7CU || state == 0x7DU) {
        out.recognized = true;
        reset_lady_components(session, out);
        (void)apply_lady_preset(
            session, 0U, LadyPlacementPreset::ActiveDeployed, out);
        set_lady_domain(session, LadyControlDomain::IndependentMotionScript, out);
    } else if (state == 0x7FU) {
        out.recognized = true;
        reset_lady_components(session, out);
        (void)apply_lady_preset(
            session, 1U, LadyPlacementPreset::ActiveDeployed, out);
        (void)apply_lady_preset(
            session, 2U, LadyPlacementPreset::ActiveDeployed, out);
    } else if (state == 0x81U) {
        out.recognized = true;
        reset_lady_components(session, out);
        if (auto* binding = lady_binding(session, 3U); binding != nullptr) {
            binding->runtime_uniform_scale = 1.0F;
        }
        (void)apply_lady_preset(
            session, 3U, LadyPlacementPreset::ActiveDeployed, out);
    } else if (state == 0x85U || state == 0x8FU) {
        out.recognized = true;
        reset_lady_components(session, out);
    }
    return out;
}

LadyRuntimeApplyResult apply_lady_signal(Session* session,
                                        std::uint16_t state,
                                        std::uint8_t lane,
                                        std::uint8_t channel,
                                        std::uint8_t value) noexcept {
    LadyRuntimeApplyResult out;
    if (session == nullptr || session->lady_component_bindings.empty() ||
        lane > 1U || channel > 4U) {
        if (session == nullptr) out.fully_materialized = false;
        return out;
    }

    const auto preset = [&](std::uint8_t component, LadyPlacementPreset p) {
        out.recognized = true;
        (void)apply_lady_preset(session, component, p, out);
    };
    const auto spawn = [&](std::int8_t actor) {
        out.recognized = true;
        out.dynamic_actor = actor;
    };
    const auto side_effect = [&]() {
        out.recognized = true;
        out.runtime_side_effect = true;
    };

    if (state == 0x2AU && lane == 0U && channel == 0U) {
        if (value == 1U) preset(1U, LadyPlacementPreset::ActiveDeployed);
        else if (value == 2U) preset(1U, LadyPlacementPreset::BodyStowed);
    } else if (state == 0x2BU && lane == 0U && channel == 0U) {
        if (value == 1U) preset(4U, LadyPlacementPreset::ActiveDeployed);
        else if (value == 2U) preset(4U, LadyPlacementPreset::BodyStowed);
    } else if (state == 0x7BU && lane == 0U && channel == 0U && value == 1U) {
        preset(0U, LadyPlacementPreset::ActiveDeployed);
        set_lady_domain(session, LadyControlDomain::BodyConstraint, out);
    } else if ((state == 0x5CU || state == 0x5DU) &&
               lane == 0U && channel == 0U && value == 1U) {
        preset(0U, LadyPlacementPreset::ActiveDeployed);
        set_lady_domain(session, LadyControlDomain::BodyConstraint, out);
    } else if ((state == 0x59U || state == 0x61U) &&
               lane == 0U && channel == 0U && value == 1U) {
        preset(0U, LadyPlacementPreset::BodyStowed);
        set_lady_domain(session, LadyControlDomain::IndependentMotionScript, out);
    } else if (in_range(state, 0x56U, 0x58U) &&
               lane == 1U && channel == 0U && value == 1U) {
        spawn(2);  // CEm034Shl02
    } else if (((in_range(state, 0x34U, 0x36U) ||
                 in_range(state, 0x3BU, 0x3DU) ||
                 in_range(state, 0x42U, 0x44U) ||
                 in_range(state, 0x49U, 0x4BU) ||
                 in_range(state, 0x50U, 0x52U)) &&
                lane == 1U && channel == 0U && value == 1U) ||
               (state == 0x7FU && lane == 1U && channel == 0U && value == 1U)) {
        spawn(0);  // CEm034Shl00
    } else if (((in_range(state, 0x53U, 0x55U) ||
                 state == 0x5AU || state == 0x5BU || state == 0x5EU ||
                 in_range(state, 0x64U, 0x73U)) &&
                lane == 1U && channel == 0U && value == 1U)) {
        spawn(1);  // CEm034Shl01
    } else if (state == 0x81U && lane == 1U) {
        if (channel == 0U && value == 1U) {
            spawn(5);
        } else if (channel == 1U) {
            // 0x140169EC2 writes 1.0 every state81 update; 0x140169EE0
            // promotes it to 1.5 only while channel1 == 1.
            out.recognized = true;
            if (auto* binding = lady_binding(session, 3U); binding != nullptr) {
                const float scale = value == 1U ? 1.5F : 1.0F;
                if (!set_lady_component_runtime_scale(session, *binding, scale)) {
                    out.fully_materialized = false;
                }
                ++out.changed_components;
            }
        } else if (channel == 2U && value == 1U) {
            preset(3U, LadyPlacementPreset::BodyStowed);
        }
    } else if (state == 0x85U && lane == 1U) {
        if (channel == 1U && value == 1U) {
            preset(4U, LadyPlacementPreset::ActiveDeployed);
        } else if (channel == 1U && value == 2U) {
            preset(4U, LadyPlacementPreset::BodyStowed);
        } else if (channel == 0U && (value == 1U || value == 2U)) {
            side_effect();
        }
    } else if (state == 0x8FU && lane == 1U &&
               channel == 0U && value == 1U) {
        spawn(4);  // CEm034Shl04
    } else if ((state == 0x60U || state == 0x63U) &&
               lane == 1U && (channel == 0U || channel == 1U) &&
               value == 1U) {
        side_effect();
    } else if (state == 0x7FU && lane == 1U && channel == 1U) {
        side_effect();
    }

    return out;
}

std::span<const EffectBinding> em034_effect_bindings() noexcept {
    // FXBANK source is em034.pac slot 28 / em034_028.pnst. The V ids are
    // runtime identities, not human effect names. Shl04 owns two distinct
    // bindings; E765 is deliberately not merged with V475 because its
    // lifecycle differs in the canonical actor path. All promoted V roots are
    // actor-owned: their retire contract is the owning Shl actor's canonical
    // lifecycle, not a guessed child timer. Reader may leave the actor
    // deferred when no exact standalone world matrix is available, but it
    // must still preserve this ownership rule for a later retire event.
    constexpr auto confirmed = EvidenceStatus::EXE_AND_CORPUS_CONFIRMED;
    static constexpr std::array<EffectChildRef, 2> kV276Children{{
        {'E', 571U, 28U, 1U,
         {0.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 0.0F}, {1.0F, 1.0F, 1.0F}, confirmed, 0},
        {'E', 571U, 28U, 1U,
         {0.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 90.0F}, {1.0F, 1.0F, 1.0F}, confirmed, 0},
    }};
    static constexpr std::array<EffectChildRef, 3> kV423Children{{
        {'E', 752U, 28U, 1U,
         {60.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 0.0F}, {1.0F, 1.0F, 1.0F}, confirmed, 0},
        {'E', 887U, 28U, 1U,
         {60.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 0.0F}, {1.0F, 1.0F, 1.0F}, confirmed, 3},
        {'P', 337U, 28U, 0U,
         {60.0F, 0.0F, 0.0F}, {0.0F, 90.0F, 0.0F}, {1.0F, 1.0F, 1.0F}, confirmed, 0},
    }};
    static constexpr std::array<EffectChildRef, 1> kV463Children{{
        {'E', 741U, 28U, 1U,
         {0.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 0.0F}, {1.0F, 1.0F, 1.0F}, confirmed, 0},
    }};
    static constexpr std::array<EffectChildRef, 1> kV475Children{{
        {'E', 404U, 28U, 1U,
         {0.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 0.0F}, {1.0F, 1.0F, 1.0F}, confirmed, 0},
    }};
    static constexpr std::array<EffectChildRef, 4> kV8Children{{
        {'E', 10U, 28U, 1U,
         {0.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 0.0F}, {2.0F, 2.0F, 2.0F}, confirmed, 0},
        {'E', 10U, 28U, 1U,
         {0.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 0.0F}, {1.0F, 1.0F, 1.0F}, confirmed, 1},
        {'E', 10U, 28U, 1U,
         {0.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 0.0F}, {1.0F, 1.0F, 1.0F}, confirmed, 2},
        {'E', 2U, 28U, 1U,
         {0.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 0.0F}, {1.0F, 1.0F, 1.0F}, confirmed, 3},
    }};
    static constexpr std::array<EffectChildRef, 6> kV488Children{{
        {'P', 18U, 28U, 0U,
         {0.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 0.0F}, {1.0F, 1.0F, 1.0F}, confirmed, 4},
        {'P', 3U, 28U, 0U,
         {0.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 0.0F}, {1.0F, 1.0F, 1.0F}, confirmed, 2},
        {'P', 16U, 28U, 0U,
         {0.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 0.0F}, {1.0F, 1.0F, 1.0F}, confirmed, 5},
        {'P', 2U, 28U, 0U,
         {0.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 0.0F}, {1.0F, 1.0F, 1.0F}, confirmed, 3},
        {'E', 1U, 28U, 1U,
         {0.0F, 15.0F, 0.0F}, {0.0F, 0.0F, 0.0F}, {1.1F, 1.1F, 1.1F}, confirmed, 0},
        {'V', 8U, 28U, 3U,
         {0.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 0.0F}, {1.0F, 1.0F, 1.0F}, confirmed, 2,
         std::span<const EffectChildRef>{kV8Children}},
    }};
    static constexpr std::array<EffectBinding, 5> kBindings{{
        {0U, 'V', 463U, 28U, RuntimeEffectParent::DynamicActor,
         0xFFFFU, 1U, 0U, 1U, EvidenceStatus::EXE_AND_CORPUS_CONFIRMED,
         EffectLifetimeRule::ParentActorRetire,
         EvidenceStatus::EXE_CONFIRMED,
         std::span<const EffectChildRef>{kV463Children}},
        {2U, 'V', 423U, 28U, RuntimeEffectParent::DynamicActor,
         0xFFFFU, 1U, 0U, 1U, EvidenceStatus::EXE_AND_CORPUS_CONFIRMED,
         EffectLifetimeRule::ParentActorRetire,
         EvidenceStatus::EXE_CONFIRMED,
         std::span<const EffectChildRef>{kV423Children}},
        {4U, 'V', 488U, 28U, RuntimeEffectParent::DynamicActor,
         0xFFFFU, 1U, 0U, 1U, EvidenceStatus::EXE_AND_CORPUS_CONFIRMED,
         EffectLifetimeRule::ParentActorRetire,
         EvidenceStatus::EXE_CONFIRMED,
         std::span<const EffectChildRef>{kV488Children}},
        {4U, 'V', 475U, 28U, RuntimeEffectParent::DynamicActor,
         0xFFFFU, 1U, 0U, 1U, EvidenceStatus::EXE_AND_CORPUS_CONFIRMED,
         EffectLifetimeRule::ParentActorRetire,
         EvidenceStatus::EXE_CONFIRMED,
         std::span<const EffectChildRef>{kV475Children}},
        {5U, 'V', 276U, 28U, RuntimeEffectParent::DynamicActor,
         0xFFFFU, 1U, 0U, 1U, EvidenceStatus::EXE_AND_CORPUS_CONFIRMED,
         EffectLifetimeRule::ParentActorRetire,
         EvidenceStatus::EXE_CONFIRMED,
         std::span<const EffectChildRef>{kV276Children}},
    }};
    return kBindings;
}

bool install_effect_bindings(Session* session) noexcept {
    if (session == nullptr) return false;

    session->script_effect_bindings.clear();

    bool is_em034 = false;
    const std::string_view archive_name = session->archive_name;
    constexpr std::string_view prefix = "em034";
    if (archive_name.size() >= prefix.size()) {
        for (std::size_t start = 0U;
             start + prefix.size() <= archive_name.size(); ++start) {
            bool match = true;
            for (std::size_t i = 0U; i < prefix.size(); ++i) {
                if (std::tolower(static_cast<unsigned char>(
                        archive_name[start + i])) != prefix[i]) {
                    match = false;
                    break;
                }
            }
            if (match) {
                is_em034 = true;
                break;
            }
        }
    }
    if (!is_em034 ||
        std::find(session->effect_bank_slots.begin(),
                  session->effect_bank_slots.end(), 28U) ==
            session->effect_bank_slots.end()) {
        return false;
    }

    try {
        return set_script_effect_bindings(session, em034_effect_bindings());
    } catch (...) {
        return false;
    }
}

const WeaponStateRecord* weapon_state_record(std::string_view class_name,
                                             std::uint8_t state) noexcept {
    if (state >= 24U) return nullptr;
    for (const auto& table : kWeaponStateTables) {
        if (table.class_name != class_name) continue;
        const auto& record = table.states[state];
        if (record.branch > 1U) return nullptr;  // 255 empty, >= 2 special poses
        return &record;
    }
    return nullptr;
}

bool set_weapon_state(Session* session, WeaponBinding& binding, std::uint8_t state) noexcept {
    if (session == nullptr || binding.part >= session->composite_parts.size()) return false;
    const auto* record = weapon_state_record(binding.class_name, state);
    if (record == nullptr) return false;
    try {
        auto& placement = session->composite_parts[binding.part].placement;
        const auto second = weapon_second_part(binding.class_name);
        if (second && !placement.node_constraints.empty()) {
            placement.node_constraints = {
                {second->first_node, record->joint,
                 attach_local_matrix(record->translation, record->rotation_xyz_radians)},
                {second->second_node, record->second_joint,
                 attach_local_matrix(record->second_translation,
                                     record->second_rotation_xyz_radians)},
            };
        } else {
            placement.attachment_selector = record->joint;
            placement.attachment_offset =
                attach_local_matrix(record->translation, record->rotation_xyz_radians);
        }
        binding.state = state;
        return true;
    } catch (...) {
        return false;
    }
}

Matrix4 weapon_offset_matrix(const WeaponAttachRecord& record) noexcept {
    return attach_local_matrix(record.translation, record.rotation_xyz_radians);
}

std::span<const EnemyVariant> enemy_variants_for(std::string_view archive_name) noexcept {
    const auto slash = archive_name.find_last_of("/\\");
    if (slash != std::string_view::npos) archive_name.remove_prefix(slash + 1U);
    constexpr std::string_view name = "em000.pac";
    if (archive_name.size() != name.size()) return {};
    for (std::size_t i = 0U; i < name.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(archive_name[i])) != name[i]) return {};
    }
    return kEm000Variants;
}

std::vector<ArchiveVariant> archive_variants(std::string_view archive_name) {
    std::vector<ArchiveVariant> out;
    for (const auto& enemy : enemy_variants_for(archive_name)) {
        ArchiveVariant first;
        first.enemy = &enemy;
        first.label = std::string{enemy.class_name};
        if (enemy.weapon_slot_alt == enemy.weapon_slot) {
            out.push_back(std::move(first));
            continue;
        }
        first.label += " A";
        out.push_back(first);
        ArchiveVariant second = first;
        second.alternate_weapon = true;
        second.label = std::string{enemy.class_name} + " B";
        out.push_back(std::move(second));
    }
    if (!out.empty()) return out;
    const auto slash = archive_name.find_last_of("/\\");
    if (slash != std::string_view::npos) archive_name.remove_prefix(slash + 1U);

    constexpr std::string_view nevan = "em028.pac";
    bool is_nevan = archive_name.size() == nevan.size();
    for (std::size_t i = 0U; is_nevan && i < nevan.size(); ++i) {
        is_nevan = std::tolower(static_cast<unsigned char>(archive_name[i])) == nevan[i];
    }
    if (is_nevan) {
        ArchiveVariant calm;
        calm.label = "Bats in";
        calm.hide_slot = 5U;
        calm.hide_objects = {2U, 3U};
        calm.hide_count = 2U;
        out.push_back(calm);
        ArchiveVariant swarm;
        swarm.label = "Bats out";
        out.push_back(swarm);
        return out;
    }

    // em034.pac (Lady) contains two complete appearance sets plus a separate
    // group of equipment/weapon MODs. Corpus evidence from the retail PAC:
    //   slots 1 + 17  -> MOD header +0x14 = 0x00000384
    //   slots 32 + 34 -> MOD header +0x14 = 0x0000063D
    //   slots 20..26 + 30 -> +0x14 = 0x000AF258 (equipment group)
    // Both appearance pairs also share the same transform/texture-domain
    // shape (23+9 nodes, two texture slots). Keeping all twelve MODs in one
    // composite is therefore structurally wrong and visibly overlays both
    // costumes while dropping equipment at source origin.
    constexpr std::string_view lady = "em034.pac";
    bool is_lady = archive_name.size() == lady.size();
    for (std::size_t i = 0U; is_lady && i < lady.size(); ++i) {
        is_lady = std::tolower(static_cast<unsigned char>(archive_name[i])) == lady[i];
    }
    if (is_lady) {
        // CEm034 runtime ownership is now EXE-confirmed:
        //   persistent components: slots20..24, owned for the actor lifetime;
        //   dynamic CShell resources: slot25 (Shl02), slots26+30 (Shl03).
        // Dynamic resources remain PAC children and are not overlaid in the
        // base Lady appearance.
        const auto bind_core = [](ArchiveVariant& variant,
                                  std::uint32_t body_slot,
                                  std::uint32_t hair_slot) {
            variant.part_attachments[0] =
                {body_slot, hair_slot, 5U, false, true};
            for (std::size_t i = 0U; i < kCEm034LadyComponents.size(); ++i) {
                const auto& component = kCEm034LadyComponents[i];
                const auto& stowed =
                    component.presets[static_cast<std::size_t>(
                        LadyPlacementPreset::BodyStowed)];
                variant.part_attachments[i + 1U] = {
                    body_slot,
                    component.model_slot,
                    stowed.serialized_node,
                    false,
                    true,
                    true,
                    stowed.translation,
                    stowed.rotation_xyz_radians,
                    true,
                };
            }
            variant.part_attachment_count =
                1U + static_cast<std::uint32_t>(kCEm034LadyComponents.size());
        };

        ArchiveVariant first;
        first.label = "Lady · costume 1";
        first.include_top_level_mod_slots =
            {1U, 17U, 20U, 21U, 22U, 23U, 24U};
        first.include_top_level_mod_count = 7U;
        first.texture_overrides = {{
            {1U, 0U}, {17U, 0U},
            {20U, 19U}, {21U, 19U}, {22U, 19U}, {23U, 19U},
            {24U, 19U},
        }};
        first.texture_override_count = 7U;
        bind_core(first, 1U, 17U);
        out.push_back(first);

        ArchiveVariant second;
        second.label = "Lady · costume 2";
        second.include_top_level_mod_slots =
            {32U, 34U, 20U, 21U, 22U, 23U, 24U};
        second.include_top_level_mod_count = 7U;
        second.texture_overrides = {{
            {32U, 31U}, {34U, 31U},
            {20U, 33U}, {21U, 33U}, {22U, 33U}, {23U, 33U},
            {24U, 33U},
        }};
        second.texture_override_count = 7U;
        bind_core(second, 32U, 34U);
        out.push_back(second);
    }
    return out;
}

std::uint32_t player_coat_host_joint(std::span<const std::uint8_t> coat_mod,
                                    std::size_t body_joint_count) noexcept {
    if (coat_mod.size() < 0x40U || coat_mod[0] != 'M' || coat_mod[1] != 'O' ||
        coat_mod[2] != 'D') {
        return kPlayerCoatHostJoint;
    }
    const std::uint32_t joint = kPlayerCoatHostJoint + coat_mod[0x13];
    return joint < body_joint_count ? joint : kPlayerCoatHostJoint;
}

Matrix4 attach_local_matrix_zyx(const std::array<float, 3>& translation,
                                const std::array<float, 3>& rotation_xyz_radians) noexcept {
    // Row-vector rotations as 0x140030F10 / 0x140030FC0 / 0x140031080 build them.
    const float cx = std::cos(rotation_xyz_radians[0]), sx = std::sin(rotation_xyz_radians[0]);
    const float cy = std::cos(rotation_xyz_radians[1]), sy = std::sin(rotation_xyz_radians[1]);
    const float cz = std::cos(rotation_xyz_radians[2]), sz = std::sin(rotation_xyz_radians[2]);
    const std::array<float, 9> rx{1.0F, 0.0F, 0.0F, 0.0F, cx, sx, 0.0F, -sx, cx};
    const std::array<float, 9> ry{cy, 0.0F, -sy, 0.0F, 1.0F, 0.0F, sy, 0.0F, cy};
    const std::array<float, 9> rz{cz, sz, 0.0F, -sz, cz, 0.0F, 0.0F, 0.0F, 1.0F};
    const auto mul = [](const std::array<float, 9>& a, const std::array<float, 9>& b) {
        std::array<float, 9> out{};
        for (std::size_t r = 0U; r < 3U; ++r) {
            for (std::size_t c = 0U; c < 3U; ++c) {
                for (std::size_t k = 0U; k < 3U; ++k) out[r * 3U + c] += a[r * 3U + k] * b[k * 3U + c];
            }
        }
        return out;
    };
    const auto m = mul(mul(rz, ry), rx);
    Matrix4 out;
    for (std::size_t r = 0U; r < 3U; ++r) {
        for (std::size_t c = 0U; c < 3U; ++c) out.values[r * 4U + c] = m[r * 3U + c];
    }
    out.values[12] = translation[0];
    out.values[13] = translation[1];
    out.values[14] = translation[2];
    return out;
}

std::optional<WeaponMotionBank> weapon_motion_bank(std::string_view archive_name) noexcept {
    const auto slash = archive_name.find_last_of("/\\");
    if (slash != std::string_view::npos) archive_name.remove_prefix(slash + 1U);
    constexpr std::string_view prefix = "pl000_00_";
    constexpr std::string_view suffix = ".pac";
    if (archive_name.size() <= prefix.size() + suffix.size()) return std::nullopt;
    const auto lower = [](char c) {
        return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    };
    for (std::size_t i = 0U; i < prefix.size(); ++i) {
        if (lower(archive_name[i]) != prefix[i]) return std::nullopt;
    }
    const auto tail = archive_name.substr(archive_name.size() - suffix.size());
    for (std::size_t i = 0U; i < suffix.size(); ++i) {
        if (lower(tail[i]) != suffix[i]) return std::nullopt;
    }
    const auto digits = archive_name.substr(prefix.size(),
                                            archive_name.size() - prefix.size() - suffix.size());
    if (digits.empty() || digits.size() > 2U) return std::nullopt;
    unsigned value = 0U;
    for (const char c : digits) {
        if (c < '0' || c > '9') return std::nullopt;
        value = value * 10U + static_cast<unsigned>(c - '0');
    }
    for (const auto& bank : kDanteWeaponMotionBanks) {
        if (bank.file_index == value) return bank;
    }
    return std::nullopt;
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
