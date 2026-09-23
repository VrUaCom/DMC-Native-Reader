#include "dmcresource/motion/motion_player.h"

#include <array>
#include <cmath>
#include <limits>
#include <new>
#include <optional>
#include <span>
#include <utility>
#include <vector>

#include "dmc_rengine/analysis/mod/animation_binding.hpp"
#include "dmc_rengine/formats/mod/world_transform.hpp"
#include "dmcresource/matrix_ops.h"
#include "dmcresource/motion/motion_clip.h"
#include "dmcresource/motion/part_attachment.h"
#include "dmcresource/resource_session.h"
#include "dmcresource/scene_projection.h"

namespace dmcresource::motion {

namespace {

namespace world = dmc::rengine::formats::mod::world_transform;
namespace animation = dmc::rengine::analysis::mod;

// DMC3 MOD vertices carry at most three influences (EXE + corpus confirmed).
struct VertexInfluences final {
    std::array<std::uint32_t, 3> node{};
    std::array<float, 3> weight{};
    std::uint8_t count{};
};

struct PartMotion final {
    std::shared_ptr<const SkeletonRig> rig;
    MotionClip clip;
    std::size_t vertex_begin{};
    std::size_t node_begin{};
    std::vector<Vec3> rest_vertices;
    std::vector<VertexInfluences> influences;
    std::vector<Matrix4f> inverse_rest;
    Matrix4 placement{};
    bool placed{false};
    std::vector<Matrix4f> locals;
};

[[nodiscard]] Vec3 transform_row_vector(const Vec3& p, const std::array<float, 16>& m) noexcept {
    return {p.x * m[0] + p.y * m[4] + p.z * m[8] + m[12],
            p.x * m[1] + p.y * m[5] + p.z * m[9] + m[13],
            p.x * m[2] + p.y * m[6] + p.z * m[10] + m[14]};
}

[[nodiscard]] std::size_t scene_vertex_count(const RenderScene& scene) noexcept {
    std::size_t total = 0U;
    for (const auto& primitive : scene.meshes) total += primitive.mesh.vertices.size();
    return total;
}

// Flatten per-primitive skin bindings into the materialized vertex order used
// by materialize_render_scene (primitive order, vertices 1:1).
[[nodiscard]] bool flatten_influences(const RenderScene& scene,
                                      std::size_t node_count,
                                      std::vector<VertexInfluences>* out) {
    out->clear();
    out->reserve(scene_vertex_count(scene));
    for (std::size_t primitive = 0U; primitive < scene.meshes.size(); ++primitive) {
        const auto& mesh = scene.meshes[primitive].mesh;
        const SkinBinding* binding = nullptr;
        for (const auto& candidate : scene.skins) {
            if (candidate.mesh_primitive == primitive &&
                candidate.vertices.size() == mesh.vertices.size()) {
                binding = &candidate;
                break;
            }
        }
        for (std::size_t vertex = 0U; vertex < mesh.vertices.size(); ++vertex) {
            VertexInfluences influences;
            if (binding != nullptr) {
                for (const auto& joint : binding->vertices[vertex].influences) {
                    if (influences.count >= influences.node.size()) break;
                    if (joint.node_index >= node_count || !std::isfinite(joint.weight) ||
                        joint.weight <= 0.0F) {
                        continue;
                    }
                    influences.node[influences.count] = joint.node_index;
                    influences.weight[influences.count] = joint.weight;
                    ++influences.count;
                }
            }
            out->push_back(influences);
        }
    }
    return true;
}

}  // namespace

struct MotionState final {
    std::string name;
    std::vector<PartMotion> parts;
    std::size_t static_parts{};
    std::vector<Vec3> source_vertices;
    std::vector<Matrix4> source_node_world;
    HierarchyOverlay source_overlay;
    float end_frame{};
    float loop_start_frame{};
};

namespace {

[[nodiscard]] std::optional<PartMotion> bind_part(const RenderScene& scene,
                                                  std::size_t vertex_begin,
                                                  std::size_t node_begin,
                                                  const CompositePlacement* placement,
                                                  std::span<const std::byte> mot,
                                                  std::string* reason) {
    if (scene.rig == nullptr) {
        *reason = to_string(ClipError::NoSkeleton);
        return std::nullopt;
    }
    if (scene.nodes.size() != scene.rig->node_count()) {
        *reason = "render-nodes-differ-from-rig";
        return std::nullopt;
    }
    std::string parse_message;
    auto clip = MotionClip::bind(mot, *scene.rig, &parse_message);
    if (!clip) {
        *reason = to_string(clip.error());
        if (!parse_message.empty()) *reason += " (" + parse_message + ")";
        return std::nullopt;
    }
    auto inverse_rest = world::build_model_space_inverse_rest_matrices(scene.rig->domain);
    if (!inverse_rest.has_value() || inverse_rest->size() != scene.rig->node_count()) {
        *reason = "inverse-rest-unavailable";
        return std::nullopt;
    }

    PartMotion part;
    part.rig = scene.rig;
    part.clip = std::move(*clip);
    part.vertex_begin = vertex_begin;
    part.node_begin = node_begin;
    part.inverse_rest = std::move(*inverse_rest);
    part.locals.resize(scene.rig->node_count());
    Mesh rest;
    if (!materialize_render_scene(scene, &rest)) {
        *reason = "rest-projection-failed";
        return std::nullopt;
    }
    part.rest_vertices = std::move(rest.vertices);
    if (!flatten_influences(scene, scene.rig->node_count(), &part.influences) ||
        part.influences.size() != part.rest_vertices.size()) {
        *reason = "skin-projection-mismatch";
        return std::nullopt;
    }
    if (placement != nullptr && placement->resolved) {
        part.placement = placement->root_matrix;
        part.placed = true;
    }
    return part;
}

}  // namespace

MotionLoadReport load_motion(Session* session,
                             std::string_view name,
                             const std::uint8_t* bytes,
                             std::size_t size) noexcept {
    MotionLoadReport report;
    if (session == nullptr || bytes == nullptr || size == 0U) {
        report.detail = "Motion: no session or empty MOT";
        return report;
    }
    try {
        clear_motion(session);
        const auto mot = std::span<const std::byte>{
            reinterpret_cast<const std::byte*>(bytes), size};
        auto state = std::make_shared<MotionState>();
        state->name = std::string{name};

        std::string reasons;
        std::size_t vertex_cursor = 0U;
        std::size_t node_cursor = 0U;
        const auto try_part = [&](const RenderScene& scene,
                                  const CompositePlacement* placement,
                                  const std::string& label) {
            std::string reason;
            auto part = bind_part(scene, vertex_cursor, node_cursor, placement, mot, &reason);
            if (part.has_value()) {
                state->parts.push_back(std::move(*part));
            } else {
                ++state->static_parts;
                if (!reasons.empty()) reasons += "; ";
                reasons += label + ": " + reason;
            }
            vertex_cursor += scene_vertex_count(scene);
            node_cursor += scene.nodes.size();
        };

        if (session->composite_parts.empty()) {
            try_part(session->scene, nullptr, "model");
        } else {
            for (const auto& part : session->composite_parts) {
                if (part.placement.mode == CompositePlacementMode::HostJointSkeleton) {
                    // Driven by its host joint (IPlayer coat), not by the MOT.
                    ++state->static_parts;
                    if (!reasons.empty()) reasons += "; ";
                    reasons += part.name + ": follows host joint " +
                               std::to_string(part.placement.attachment_selector);
                    vertex_cursor += scene_vertex_count(part.scene);
                    node_cursor += part.scene.nodes.size();
                    continue;
                }
                try_part(part.scene, &part.placement, part.name);
            }
        }

        if (vertex_cursor != session->render_mesh.vertices.size() ||
            node_cursor != session->scene.nodes.size()) {
            report.detail = "Motion: session projection does not match its model parts";
            return report;
        }
        if (state->parts.empty()) {
            report.detail = "Motion " + std::string{name} +
                            " could not drive any model part: " + reasons;
            return report;
        }

        state->source_vertices = session->render_mesh.vertices;
        state->source_node_world.reserve(session->scene.nodes.size());
        for (const auto& node : session->scene.nodes) {
            state->source_node_world.push_back(node.world);
        }
        state->source_overlay = session->hierarchy_overlay;
        state->end_frame = state->parts.front().clip.end_frame();
        state->loop_start_frame = state->parts.front().clip.loop_start_frame();

        const auto& stats = state->parts.front().clip.stats();
        report.ok = true;
        report.animated_parts = state->parts.size();
        report.static_parts = state->static_parts;
        report.end_frame = state->end_frame;
        report.detail = "Motion " + std::string{name} +
            ": animatedParts=" + std::to_string(state->parts.size()) +
            " staticParts=" + std::to_string(state->static_parts) +
            " endFrame=" + std::to_string(static_cast<int>(state->end_frame)) +
            " tracks=" + std::to_string(stats.tracks) +
            " comp3=" + std::to_string(stats.compression3_tracks) +
            " comp2=" + std::to_string(stats.compression2_tracks) +
            " heldAtRest=" + std::to_string(stats.unsupported_tracks) +
            " local=EXE 0x140310310 world=local*parent skin=inverseRest*world";
        if (!reasons.empty()) report.detail += "\nStatic parts: " + reasons;

        session->motion = std::move(state);
        if (!apply_motion_frame(session, 0.0F)) {
            clear_motion(session);
            report = {};
            report.detail = "Motion: first frame could not be evaluated";
        }
        return report;
    } catch (const std::bad_alloc&) {
        report = {};
        report.detail = "Motion: allocation failed";
        return report;
    } catch (...) {
        report = {};
        report.detail = "Motion: unexpected failure";
        return report;
    }
}

bool apply_motion_frame(Session* session, float frame) noexcept {
    if (session == nullptr || session->motion == nullptr || !std::isfinite(frame)) return false;
    try {
        auto& state = *session->motion;
        auto& vertices = session->render_mesh.vertices;
        const Matrix4f identity = world::identity_matrix();

        for (auto& part : state.parts) {
            if (!part.clip.evaluate_locals(frame, part.locals)) return false;
            const auto current = animation::build_animated_world_matrices(
                part.rig->domain, part.locals, identity);
            if (!current.has_value() || current->size() != part.inverse_rest.size()) return false;

            std::vector<Matrix4f> palette(current->size());
            for (std::size_t node = 0U; node < current->size(); ++node) {
                palette[node] = world::multiply_dmc3_matrices(
                    part.inverse_rest[node], (*current)[node]);
            }

            if (part.vertex_begin + part.rest_vertices.size() > vertices.size() ||
                part.node_begin + current->size() > session->scene.nodes.size()) {
                return false;
            }
            for (std::size_t index = 0U; index < part.rest_vertices.size(); ++index) {
                const auto& rest = part.rest_vertices[index];
                const auto& influences = part.influences[index];
                Vec3 skinned = rest;
                float total = 0.0F;
                for (std::uint8_t k = 0U; k < influences.count; ++k) total += influences.weight[k];
                if (influences.count > 0U && total > 0.0F) {
                    skinned = {};
                    for (std::uint8_t k = 0U; k < influences.count; ++k) {
                        const auto moved = transform_row_vector(
                            rest, palette[influences.node[k]].values);
                        const float w = influences.weight[k] / total;
                        skinned.x += moved.x * w;
                        skinned.y += moved.y * w;
                        skinned.z += moved.z * w;
                    }
                }
                if (part.placed) skinned = transform_row_vector(skinned, part.placement.values);
                if (!std::isfinite(skinned.x) || !std::isfinite(skinned.y) ||
                    !std::isfinite(skinned.z)) {
                    return false;
                }
                vertices[part.vertex_begin + index] = skinned;
            }
            for (std::size_t node = 0U; node < current->size(); ++node) {
                Matrix4 placed_world;
                placed_world.values = (*current)[node].values;
                if (part.placed) {
                    Matrix4 composed;
                    if (matrix_ops::multiply(placed_world, part.placement, &composed)) {
                        placed_world = composed;
                    }
                }
                session->scene.nodes[part.node_begin + node].world = placed_world;
            }
        }
        // Parts hanging from a host joint follow the freshly posed host.
        (void)apply_part_attachments(session);
        HierarchyOverlay overlay;
        if (materialize_hierarchy_overlay(session->scene, &overlay)) {
            session->hierarchy_overlay = std::move(overlay);
        }
        return true;
    } catch (...) {
        return false;
    }
}

void clear_motion(Session* session) noexcept {
    if (session == nullptr || session->motion == nullptr) return;
    auto state = std::move(session->motion);
    session->motion.reset();
    if (state->source_vertices.size() == session->render_mesh.vertices.size()) {
        session->render_mesh.vertices = std::move(state->source_vertices);
    }
    if (state->source_node_world.size() == session->scene.nodes.size()) {
        for (std::size_t node = 0U; node < session->scene.nodes.size(); ++node) {
            session->scene.nodes[node].world = state->source_node_world[node];
        }
    }
    session->hierarchy_overlay = std::move(state->source_overlay);
}

bool has_motion(const Session* session) noexcept {
    return session != nullptr && session->motion != nullptr;
}

float motion_end_frame(const Session* session) noexcept {
    return has_motion(session) ? session->motion->end_frame : 0.0F;
}

float motion_loop_start_frame(const Session* session) noexcept {
    return has_motion(session) ? session->motion->loop_start_frame : 0.0F;
}

std::span<const Vec3> motion_rest_vertices(const Session* session) noexcept {
    if (!has_motion(session)) return {};
    return session->motion->source_vertices;
}

}  // namespace dmcresource::motion
