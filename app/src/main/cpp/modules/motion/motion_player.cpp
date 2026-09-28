#include "dmcresource/motion/motion_player.h"

#include <algorithm>
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
#include "dmcresource/motion/motion_script.h"
#include "dmcresource/motion/part_attachment.h"
#include "dmcresource/motion/uv_scroll.h"
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
    // Last evaluated frame; cloth advances one solver step per game frame.
    float last_frame{-1.0F};
    // Game-frame clock for TSC scrolls (keeps running across loops).
    float scroll_clock{};
    // Weapon attach states from the motion script (pl000.pac slot 5).
    std::vector<WeaponStateKey> weapon_keys;

    // Script Play is distinct from raw MOT playback.
    bool script_driven{};
    Session::MotionScriptRole script_role{Session::MotionScriptRole::Primary};
    std::uint32_t script_slot{};
    std::size_t script_bank{};
    std::size_t script_action{};
    std::optional<std::uint16_t> lady_state;
    std::uint8_t lady_lane_mask{0x3U};
    std::vector<ScriptSignalKey> script_signals;
    std::array<std::vector<ScriptSignalKey>, 2> lady_lane_signals;
    float lady_runtime_frame{-1.0F};
    bool lady_entry_applied{};
    std::int8_t last_dynamic_actor{-1};
};

namespace {

struct ScriptMotionSelection final {
    std::size_t bank{};
    std::size_t action{};
    std::uint16_t resource_id{};
};

[[nodiscard]] bool is_em034(const Session& session) noexcept {
    return session.archive_name.find("em034") != std::string::npos;
}

[[nodiscard]] std::vector<MotionPack> session_motion_packs(const Session& session) {
    std::vector<MotionPack> packs;
    for (const auto& motion : session.motion_library) {
        if (motion.pack_slot < 0 || motion.mot_slot < 0) continue;
        const auto slot = static_cast<std::uint32_t>(motion.pack_slot);
        auto it = std::find_if(
            packs.begin(), packs.end(),
            [slot](const MotionPack& p) { return p.archive_slot == slot; });
        if (it == packs.end()) {
            packs.push_back({slot, {}});
            it = std::prev(packs.end());
        }
        it->slots.push_back(static_cast<std::uint32_t>(motion.mot_slot));
    }
    return packs;
}

[[nodiscard]] std::vector<std::uint16_t> lady_groups_for_pack(
    const Session& session,
    const Session::MotionScriptBinding& binding,
    const Session::MotionPayload& motion) {
    std::vector<std::uint16_t> groups;
    if (!is_em034(session) || motion.pack_slot < 0) return groups;

    if (binding.role == Session::MotionScriptRole::LadyComponent0) {
        // EXE_AND_CORPUS_CONFIRMED:
        // em034_013 bank4 -> top-level PAC slot11.
        if (motion.pack_slot == 11) groups.push_back(4U);
        return groups;
    }

    if (binding.role == Session::MotionScriptRole::LadyBody) {
        // EXE-confirmed CEm034 body pack map (shared pack reuse is valid):
        //   group1 -> slot3
        //   group2 -> slot4
        //   group3 -> slot5
        //   group4 -> slot6
        //   group6 -> slot6
        // group0 is intentionally left unbound.
        switch (motion.pack_slot) {
        case 3: groups.push_back(1U); break;
        case 4: groups.push_back(2U); break;
        case 5: groups.push_back(3U); break;
        case 6:
            groups.push_back(4U);
            groups.push_back(6U);
            break;
        default:
            break;
        }
    }
    return groups;
}

[[nodiscard]] std::optional<ScriptMotionSelection> pick_script_motion(
    const Session& session,
    const Session::MotionScriptBinding& binding,
    const Session::MotionPayload& motion) {
    if (binding.script == nullptr || motion.mot_slot < 0) return std::nullopt;

    auto groups = lady_groups_for_pack(session, binding, motion);
    if (groups.empty() && motion.pack_slot >= 0) {
        const auto packs = session_motion_packs(session);
        for (const auto& g : bind_motion_groups(
                 *binding.script, packs, session.archive_name)) {
            if (g.archive_slot &&
                static_cast<int>(*g.archive_slot) == motion.pack_slot) {
                groups.push_back(g.group);
            }
        }
    }

    for (const auto group : groups) {
        const auto slot = static_cast<std::uint16_t>(motion.mot_slot);
        if (group > 655U || slot >= 100U) continue;
        const auto id = static_cast<std::uint16_t>(group * 100U + slot);
        const auto actions = binding.script->actions_for(id);
        if (actions.empty()) continue;

        const ScriptAction* pick = &actions.front();
        for (const auto& action : actions) {
            if (action.bank == group && action.action == slot) {
                pick = &action;
                break;
            }
            if (action.bank == group && pick->bank != group) {
                pick = &action;
            } else if (action.action == slot && pick->action != slot) {
                pick = &action;
            }
        }
        return ScriptMotionSelection{pick->bank, pick->action, id};
    }
    return std::nullopt;
}

[[nodiscard]] LadyComponentBinding* component0_binding(Session* session) noexcept {
    if (session == nullptr) return nullptr;
    for (auto& binding : session->lady_component_bindings) {
        if (binding.component == 0U) return &binding;
    }
    return nullptr;
}

[[nodiscard]] LadyComponentBinding* lady_component_binding(
    Session* session, std::uint8_t component) noexcept {
    if (session == nullptr) return nullptr;
    for (auto& binding : session->lady_component_bindings) {
        if (binding.component == component) return &binding;
    }
    return nullptr;
}

[[nodiscard]] std::optional<Matrix4> lady_component_node_world(
    const Session& session, std::uint8_t component, std::size_t local_node) noexcept {
    const LadyComponentBinding* binding = nullptr;
    for (const auto& candidate : session.lady_component_bindings) {
        if (candidate.component == component) {
            binding = &candidate;
            break;
        }
    }
    if (binding == nullptr || binding->part >= session.composite_parts.size()) {
        return std::nullopt;
    }
    const auto& part = session.composite_parts[binding->part];
    if (local_node >= part.scene.nodes.size()) return std::nullopt;

    std::size_t begin = 0U;
    for (std::size_t i = 0U; i < binding->part; ++i) {
        if (session.composite_parts[i].scene.nodes.size() >
            std::numeric_limits<std::size_t>::max() - begin) {
            return std::nullopt;
        }
        begin += session.composite_parts[i].scene.nodes.size();
    }
    if (begin + local_node >= session.scene.nodes.size()) return std::nullopt;
    return session.scene.nodes[begin + local_node].world;
}

struct Vec4f final {
    float x{};
    float y{};
    float z{};
    float w{1.0F};
};

[[nodiscard]] Vec4f transform_row4(
    const Vec4f& v, const Matrix4& m) noexcept {
    return {
        v.x * m.values[0] + v.y * m.values[4] +
            v.z * m.values[8] + v.w * m.values[12],
        v.x * m.values[1] + v.y * m.values[5] +
            v.z * m.values[9] + v.w * m.values[13],
        v.x * m.values[2] + v.y * m.values[6] +
            v.z * m.values[10] + v.w * m.values[14],
        v.x * m.values[3] + v.y * m.values[7] +
            v.z * m.values[11] + v.w * m.values[15],
    };
}

[[nodiscard]] Vec3 cross3(const Vec3& a, const Vec3& b) noexcept {
    return {
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x,
    };
}

[[nodiscard]] float length_sq3(const Vec3& v) noexcept {
    return v.x * v.x + v.y * v.y + v.z * v.z;
}

[[nodiscard]] Vec3 normalize3(Vec3 v, Vec3 fallback) noexcept {
    float len2 = length_sq3(v);
    if (!(len2 > 1.0e-15F) || !std::isfinite(len2)) {
        v = fallback;
        len2 = length_sq3(v);
    }
    if (!(len2 > 1.0e-15F) || !std::isfinite(len2)) return {1.0F, 0.0F, 0.0F};
    const float inv = 1.0F / std::sqrt(len2);
    return {v.x * inv, v.y * inv, v.z * inv};
}

[[nodiscard]] Matrix4 normalized_runtime_basis(const Matrix4& source) noexcept {
    Matrix4 out = source;
    for (std::size_t row = 0U; row < 3U; ++row) {
        const std::size_t base = row * 4U;
        const Vec3 n = normalize3(
            {source.values[base + 0U],
             source.values[base + 1U],
             source.values[base + 2U]},
            row == 0U ? Vec3{1.0F, 0.0F, 0.0F}
                      : (row == 1U ? Vec3{0.0F, 1.0F, 0.0F}
                                   : Vec3{0.0F, 0.0F, 1.0F}));
        out.values[base + 0U] = n.x;
        out.values[base + 1U] = n.y;
        out.values[base + 2U] = n.z;
        out.values[base + 3U] = 0.0F;
    }
    out.values[15] = 1.0F;
    return out;
}

[[nodiscard]] Vec3 robust_cross(
    Vec3 a, Vec3 b, bool a_cross_b) noexcept {
    Vec3 out = a_cross_b ? cross3(a, b) : cross3(b, a);
    if (length_sq3(out) > 1.0e-15F) return normalize3(out, {1.0F, 0.0F, 0.0F});
    // The EXE perturbs the reference axis in 0.1 steps when the vectors are
    // nearly parallel. Preserve that behavior instead of choosing a fixed
    // arbitrary perpendicular axis.
    b.x += 0.1F;
    out = a_cross_b ? cross3(a, b) : cross3(b, a);
    if (length_sq3(out) <= 1.0e-15F) {
        b.y += 0.1F;
        out = a_cross_b ? cross3(a, b) : cross3(b, a);
    }
    if (length_sq3(out) <= 1.0e-15F) {
        b.z += 0.1F;
        out = a_cross_b ? cross3(a, b) : cross3(b, a);
    }
    return normalize3(out, {1.0F, 0.0F, 0.0F});
}

[[nodiscard]] Matrix4 shl02_actor_matrix(
    const Matrix4& slot20_node0) noexcept {
    // 0x14016F610: (1,0,0,1) * slot20 node0 current world.
    const auto d4 = transform_row4({1.0F, 0.0F, 0.0F, 1.0F}, slot20_node0);
    const Vec3 direction = normalize3({d4.x, d4.y, d4.z}, {1.0F, 0.0F, 0.0F});
    const Vec3 up = normalize3({0.0F, 1.0F, 0.0F}, {0.0F, 1.0F, 0.0F});
    // 0x14032FD90:
    // row0 = normalize(up x direction)
    // row1 = normalize(direction x row0)
    // row2 = normalize(direction)
    const Vec3 row0 = robust_cross(up, direction, true);
    const Vec3 row1 = normalize3(cross3(direction, row0), {0.0F, 1.0F, 0.0F});

    Matrix4 out;
    out.values = {
        row0.x, row0.y, row0.z, 0.0F,
        row1.x, row1.y, row1.z, 0.0F,
        direction.x, direction.y, direction.z, 0.0F,
        slot20_node0.values[12],
        slot20_node0.values[13],
        slot20_node0.values[14],
        1.0F,
    };
    return out;
}

struct Shl03Spawn final {
    Matrix4 world{};
    Vec3 velocity{};
};

[[nodiscard]] std::optional<Shl03Spawn> shl03_spawn(
    const Session& session) noexcept {
    const auto node0 = lady_component_node_world(session, 0U, 0U);
    const auto node1 = lady_component_node_world(session, 0U, 1U);
    if (!node0.has_value() || !node1.has_value()) return std::nullopt;

    // 0x14016CBB0..0x14016CC41:
    // spawn = (87.8,0,4.28,1) * node0World + node1World.translation.
    const auto offset = transform_row4(
        {87.80000305F, 0.0F, 4.28000021F, 1.0F}, *node0);
    const Vec3 spawn{
        offset.x + node1->values[12],
        offset.y + node1->values[13],
        offset.z + node1->values[14],
    };

    // Direction/velocity domain = 50 * ((1,0,0,1) * node0World).
    const auto d4 = transform_row4(
        {1.0F, 0.0F, 0.0F, 1.0F}, *node0);
    const Vec3 velocity{50.0F * d4.x, 50.0F * d4.y, 50.0F * d4.z};
    const Vec3 direction = normalize3(velocity, {1.0F, 0.0F, 0.0F});
    const Vec3 ref{0.0F, 1.0F, 0.0F};

    // 0x14032F1D0:
    // row0 = direction
    // row1 = normalize(ref x direction)
    // row2 = normalize(direction x row1)
    const Vec3 row1 = robust_cross(ref, direction, true);
    const Vec3 row2 = normalize3(cross3(direction, row1), {0.0F, 0.0F, 1.0F});

    Shl03Spawn out;
    out.velocity = velocity;
    out.world.values = {
        direction.x, direction.y, direction.z, 0.0F,
        row1.x, row1.y, row1.z, 0.0F,
        row2.x, row2.y, row2.z, 0.0F,
        spawn.x, spawn.y, spawn.z, 1.0F,
    };
    return out;
}

void deactivate_lady_dynamic_visuals(Session* session) noexcept {
    if (session == nullptr) return;
    for (auto& visual : session->lady_dynamic_visuals) {
        visual.active = false;
        visual.spawn_frame = -1.0F;
        visual.last_update_frame = -1.0F;
        visual.retire_frame = -1.0F;
        visual.velocity = {};
        visual.world = Matrix4{};
    }
}

void spawn_lady_dynamic_visual(
    Session* session, std::int8_t actor, float event_frame) noexcept {
    if (session == nullptr || actor < 0) return;

    if (actor == 2) {
        const auto node0 = lady_component_node_world(*session, 0U, 0U);
        if (!node0.has_value()) return;
        const Matrix4 world_matrix = shl02_actor_matrix(*node0);
        for (auto& visual : session->lady_dynamic_visuals) {
            if (visual.actor != 2U) continue;
            visual.world = world_matrix;
            // Exact post-spawn steering is world-context dependent:
            // Shl02 state1 calls 0x140244870 with a live gameplay target
            // selected through the global runtime manager. The standalone
            // Reader has no authoritative target, so preserve the exact spawn
            // pose instead of inventing a straight-line projectile path.
            visual.velocity = {};
            visual.spawn_frame = event_frame;
            visual.last_update_frame = event_frame;
            // Then state2 starts +0xD68=3.0 and only promotes to state3 after
            // the subtraction becomes negative: 3->2->1->0->-1. The next
            // actor update dispatches state3 through the retire path. With
            // default delta1 this is six updates from spawn, not three.
            visual.retire_frame = event_frame + 6.0F;
            visual.active = true;
        }
        return;
    }

    if (actor == 3) {
        const auto spawn = shl03_spawn(*session);
        if (!spawn.has_value()) return;
        for (auto& visual : session->lady_dynamic_visuals) {
            if (visual.actor != 3U) continue;
            visual.world = spawn->world;
            visual.velocity = spawn->velocity;
            visual.spawn_frame = event_frame;
            visual.last_update_frame = event_frame;
            visual.retire_frame = -1.0F;
            visual.active = true;
        }
    }
}

void advance_lady_dynamic_visuals(Session* session, float frame) noexcept {
    if (session == nullptr) return;
    for (auto& visual : session->lady_dynamic_visuals) {
        if (!visual.active || visual.spawn_frame < 0.0F) continue;
        const float previous =
            visual.last_update_frame >= visual.spawn_frame
                ? visual.last_update_frame
                : visual.spawn_frame;
        if (visual.actor == 2U) {
            // No standalone translation is applied. The EXE refreshes the
            // direction from a live gameplay target before integrating state1;
            // freezing at the exact spawn pose is evidence-safe, while a
            // fabricated straight trajectory is not.
        } else if (visual.actor == 3U) {
            const float dt = std::max(frame - previous, 0.0F);
            // Shl03 state1, 0x140174D39:
            // actor+0x80 += actor+0x140 * actor+0x14.
            visual.world.values[12] += visual.velocity.x * dt;
            visual.world.values[13] += visual.velocity.y * dt;
            visual.world.values[14] += visual.velocity.z * dt;
        }
        visual.last_update_frame = frame;
        if (visual.retire_frame >= 0.0F && frame >= visual.retire_frame) {
            visual.active = false;
            if (session->effect_runtime != nullptr) {
                session->effect_runtime->record_actor_event(
                    static_cast<std::int8_t>(visual.actor), false, frame);
                session->effect_runtime->retire_owner(
                    static_cast<std::int8_t>(visual.actor), frame);
            }
        }
    }
}

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

void reset_lady_runtime(Session* session) noexcept {
    if (session == nullptr) return;
    deactivate_lady_dynamic_visuals(session);
    if (session->effect_runtime != nullptr) {
        session->effect_runtime->reset();
    }
    for (auto& binding : session->lady_component_bindings) {
        (void)set_lady_component_preset(
            session, binding, LadyPlacementPreset::BodyStowed);
        binding.runtime_uniform_scale = 1.0F;
        if (binding.component == 0U) {
            (void)set_lady_component_control_domain(
                session, binding, LadyControlDomain::BodyConstraint);
        }
    }
}

[[nodiscard]] bool apply_lady_script_runtime(Session* session,
                                             MotionState& state,
                                             float frame) noexcept {
    if (session == nullptr || !state.script_driven ||
        state.script_role != Session::MotionScriptRole::LadyBody ||
        !state.lady_state.has_value()) {
        return true;
    }

    const bool replay = !state.lady_entry_applied ||
                        frame < state.lady_runtime_frame;
    if (replay) {
        // The 144-state entry dispatcher is authoritative for the actor state.
        // apply_lady_state_entry only models states with recovered equipment
        // side effects, so an unrecognized result means "no equipment change",
        // not "invalid Lady state".
        deactivate_lady_dynamic_visuals(session);
        if (session->effect_runtime != nullptr) {
            session->effect_runtime->reset();
        }
        const auto entry = apply_lady_state_entry(session, *state.lady_state);
        state.lady_entry_applied = true;
        state.lady_runtime_frame = -1.0F;
        if (entry.dynamic_actor >= 0) {
            state.last_dynamic_actor = entry.dynamic_actor;
            spawn_lady_dynamic_visual(session, entry.dynamic_actor, 0.0F);
            if (session->effect_runtime != nullptr) {
                session->effect_runtime->record_actor_event(
                    entry.dynamic_actor, true, 0.0F);
            }
        }
    }

    // State0x81/action46 rebuilds +0x4400 to 1.0 every actor update, and
    // promotes it to 1.5 only on the channel1 pulse. Reproduce that pulse
    // rather than incorrectly making 1.5 a sticky placement state.
    if (*state.lady_state == 0x81U) {
        for (auto& binding : session->lady_component_bindings) {
            if (binding.component == 3U) {
                (void)set_lady_component_runtime_scale(session, binding, 1.0F);
                break;
            }
        }
    }

    float latest_scale_pulse = -std::numeric_limits<float>::infinity();
    for (std::uint8_t lane = 0U; lane < 2U; ++lane) {
        for (const auto& signal : state.lady_lane_signals[lane]) {
            if (!(frame > signal.after_frame)) continue;
            if (!replay && signal.after_frame < state.lady_runtime_frame) continue;

            for (std::uint8_t channel = 0U;
                 channel < signal.channels.size();
                 ++channel) {
                const auto value = signal.channels[channel];
                if (value == 0U &&
                    !(*state.lady_state == 0x81U && lane == 1U &&
                      channel == 1U)) {
                    continue;
                }
                const auto applied = apply_lady_signal(
                    session, *state.lady_state, lane, channel, value);
                if (applied.dynamic_actor >= 0) {
                    state.last_dynamic_actor = applied.dynamic_actor;
                    const float event_frame =
                        std::max(signal.after_frame, 0.0F);
                    spawn_lady_dynamic_visual(
                        session, applied.dynamic_actor, event_frame);

                    if (session->effect_runtime != nullptr) {
                        session->effect_runtime->record_actor_event(
                            applied.dynamic_actor, true, event_frame);

                        Matrix4 exact_effect_world;
                        const Matrix4* effect_world = nullptr;
                        if (applied.dynamic_actor == 2) {
                            // CEm034 calls V423 through 0x1402E7A90 mode3
                            // with slot20 current matrix: normalize the 3x3
                            // basis while preserving its translation.
                            const auto node0 =
                                lady_component_node_world(*session, 0U, 0U);
                            if (node0.has_value()) {
                                exact_effect_world =
                                    normalized_runtime_basis(*node0);
                                effect_world = &exact_effect_world;
                            }
                        }

                        (void)spawn_lady_actor_effects(
                            *session->effect_runtime,
                            applied.dynamic_actor,
                            *state.lady_state,
                            lane,
                            channel,
                            value,
                            event_frame,
                            effect_world);
                    }
                }
                if (*state.lady_state == 0x81U && lane == 1U &&
                    channel == 1U && value == 1U) {
                    latest_scale_pulse =
                        std::max(latest_scale_pulse, signal.after_frame);
                }
            }
        }
    }

    if (*state.lady_state == 0x81U &&
        std::isfinite(latest_scale_pulse) &&
        frame > latest_scale_pulse + 1.0F) {
        for (auto& binding : session->lady_component_bindings) {
            if (binding.component == 3U) {
                (void)set_lady_component_runtime_scale(session, binding, 1.0F);
                break;
            }
        }
    }

    advance_lady_dynamic_visuals(session, frame);
    if (session->effect_runtime != nullptr) {
        session->effect_runtime->update(frame);
    }
    state.lady_runtime_frame = frame;
    return true;
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
        // Motion script: weapon attach state per frame (0x1401F01F0).
        if (session->motion_script != nullptr) {
            for (const auto& payload : session->motion_library) {
                if (payload.name != name || payload.bank < 0 || payload.index < 0) continue;
                state->weapon_keys = session->motion_script->weapon_states_for_motion(
                    static_cast<std::size_t>(payload.bank), static_cast<std::size_t>(payload.index));
                break;
            }
        }

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
                    reasons += part.placement.node_constraints.empty()
                        ? part.name + ": follows host joint " +
                              std::to_string(part.placement.attachment_selector)
                        : part.name + ": follows " +
                              std::to_string(part.placement.node_constraints.size()) +
                              " host joints (node constraints)";
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

MotionLoadReport load_library_motion(Session* session,
                                     std::size_t motion_index) noexcept {
    MotionLoadReport report;
    if (session == nullptr || motion_index >= session->motion_library.size()) {
        report.detail = "Motion: invalid library index";
        return report;
    }
    try {
        clear_motion(session);
        const auto payload = session->motion_library[motion_index];

        // Raw MOT stays raw: no MotionScript state/channel execution. For the
        // slot20-only PAC11 motions we only release the model from its body
        // constraint so its own three-node MOT can be evaluated.
        if (is_em034(*session) && payload.pack_slot == 11) {
            auto* component = component0_binding(session);
            if (component == nullptr ||
                !set_lady_component_preset(
                    session, *component, LadyPlacementPreset::BodyStowed) ||
                !set_lady_component_control_domain(
                    session, *component,
                    LadyControlDomain::IndependentMotionScript)) {
                report.detail =
                    "Motion: Lady slot20 could not enter standalone MOT domain";
                return report;
            }
        }

        report = load_motion(
            session, payload.name, payload.bytes.data(), payload.bytes.size());
        if (report.ok && is_em034(*session) && payload.pack_slot == 11) {
            report.detail = "Raw component MOT (no MotionScript events): " +
                            report.detail;
        }
        return report;
    } catch (...) {
        report = {};
        report.detail = "Motion: raw library playback failed";
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
        // Parts hanging from a host joint follow the freshly posed host; chain
        // nodes advance by the frames elapsed (dt 1 per 60 fps frame, at most
        // 6 so a seek does not explode the solver; a loop restart is 1).
        std::uint32_t cloth_steps = 30U;
        if (state.last_frame < 0.0F) {
            // New motion: restart the chains from the first frame's rest pose.
            reset_part_cloth(session);
        } else {
            const float delta = frame - state.last_frame;
            cloth_steps = delta < 0.0F
                ? 1U
                : static_cast<std::uint32_t>(std::min(std::lround(delta), 6L));
        }
        if (state.last_frame >= 0.0F) {
            const float delta = frame - state.last_frame;
            state.scroll_clock += delta < 0.0F ? 1.0F : delta;
        }
        state.last_frame = frame;
        // Weapon in hand / on the back as the motion script says; before the
        // first key the default state 0 (idle record) holds.
        if (!session->weapon_bindings.empty()) {
            const auto wanted = weapon_state_at(state.weapon_keys, frame);
            for (auto& binding : session->weapon_bindings) {
                if (wanted != binding.state) (void)set_weapon_state(session, binding, wanted);
            }
        }
        // Resolve current body-driven equipment before Lady runtime signals
        // query slot20 node worlds for Shl spawn transforms. This pass does not
        // advance cloth; the final attachment pass below performs the actual
        // frame's cloth steps.
        if (!session->lady_component_bindings.empty()) {
            (void)apply_part_attachments(session, 0U);
        }
        if (!apply_lady_script_runtime(session, state, frame)) return false;
        (void)apply_part_attachments(session, cloth_steps);
        (void)apply_uv_scrolls(session, state.scroll_clock);
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
    (void)apply_uv_scrolls(session, 0.0F);
    // Back to the idle attach record.
    bool moved = false;
    for (auto& binding : session->weapon_bindings) {
        if (binding.state != 0U) moved = set_weapon_state(session, binding, 0U) || moved;
    }
    if (moved) (void)apply_part_attachments(session);
    if (!session->lady_component_bindings.empty()) {
        reset_lady_runtime(session);
        (void)apply_part_attachments(session);
    }
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

bool motion_can_drive(const Session& session, std::span<const std::uint8_t> mot) noexcept {
    try {
        const std::span<const std::byte> bytes{reinterpret_cast<const std::byte*>(mot.data()), mot.size()};
        const auto fits = [&bytes](const RenderScene& scene) {
            return scene.rig != nullptr && scene.nodes.size() == scene.rig->node_count() &&
                   MotionClip::bind(bytes, *scene.rig).has_value();
        };
        if (session.composite_parts.empty()) return fits(session.scene);
        for (const auto& part : session.composite_parts) {
            if (part.placement.mode == CompositePlacementMode::HostJointSkeleton) continue;
            if (fits(part.scene)) return true;
        }
        return false;
    } catch (...) {
        return false;
    }
}

std::size_t motion_script_count(const Session* session) noexcept {
    return session != nullptr ? session->motion_scripts.size() : 0U;
}

std::uint32_t motion_script_slot(const Session* session,
                                 std::size_t script_index) noexcept {
    if (session == nullptr || script_index >= session->motion_scripts.size()) {
        return std::numeric_limits<std::uint32_t>::max();
    }
    return session->motion_scripts[script_index].archive_slot;
}

bool motion_script_can_play_motion(const Session* session,
                                   std::size_t script_index,
                                   std::size_t motion_index) noexcept {
    if (session == nullptr ||
        script_index >= session->motion_scripts.size() ||
        motion_index >= session->motion_library.size()) {
        return false;
    }
    try {
        const auto& payload = session->motion_library[motion_index];
        if (std::any_of(
                payload.script_links.begin(), payload.script_links.end(),
                [script_index](const Session::MotionPayload::ScriptLink& link) {
                    return link.script_index == script_index;
                })) {
            return true;
        }
        return pick_script_motion(
                   *session,
                   session->motion_scripts[script_index],
                   payload)
            .has_value();
    } catch (...) {
        return false;
    }
}

MotionLoadReport load_scripted_motion(Session* session,
                                      std::size_t script_index,
                                      std::size_t motion_index) noexcept {
    MotionLoadReport report;
    if (session == nullptr ||
        script_index >= session->motion_scripts.size() ||
        motion_index >= session->motion_library.size()) {
        report.detail = "MotionScript: invalid script or MOT index";
        return report;
    }
    try {
        clear_motion(session);
        const auto binding = session->motion_scripts[script_index];
        const auto payload = session->motion_library[motion_index];

        const Session::MotionPayload::ScriptLink* materialized_link = nullptr;
        for (const auto& link : payload.script_links) {
            if (link.script_index == script_index) {
                materialized_link = &link;
                break;
            }
        }

        std::optional<ScriptMotionSelection> selection;
        if (materialized_link != nullptr) {
            selection = ScriptMotionSelection{
                materialized_link->bank,
                materialized_link->action,
                0U,
            };
        } else {
            selection = pick_script_motion(*session, binding, payload);
        }
        if (!selection.has_value() || binding.script == nullptr) {
            report.detail =
                "MotionScript slot" + std::to_string(binding.archive_slot) +
                " does not reference " + payload.name;
            return report;
        }

        if (binding.role == Session::MotionScriptRole::LadyComponent0) {
            auto* component = component0_binding(session);
            if (component == nullptr) {
                report.detail = "MotionScript: Lady component0 binding is unavailable";
                return report;
            }
            const auto initial =
                selection->bank == 4U &&
                (selection->action == 41U || selection->action == 42U)
                    ? LadyPlacementPreset::ActiveDeployed
                    : LadyPlacementPreset::BodyStowed;
            if (!set_lady_component_preset(session, *component, initial) ||
                !set_lady_component_control_domain(
                    session, *component,
                    LadyControlDomain::IndependentMotionScript)) {
                report.detail =
                    "MotionScript: Lady component0 could not enter independent control";
                return report;
            }
        }

        report = load_motion(
            session, payload.name, payload.bytes.data(), payload.bytes.size());
        if (!report.ok || session->motion == nullptr) return report;

        auto& state = *session->motion;
        state.script_driven = true;
        state.script_role = binding.role;
        state.script_slot = binding.archive_slot;
        state.script_bank = selection->bank;
        state.script_action = selection->action;
        state.script_signals =
            binding.script->signals(selection->bank, selection->action);

        if (binding.role == Session::MotionScriptRole::LadyBody) {
            std::optional<LadyBodyScriptState> mapped;
            if (materialized_link != nullptr &&
                materialized_link->lady_state >= 0) {
                mapped = LadyBodyScriptState{
                    static_cast<std::uint16_t>(materialized_link->lady_state),
                    materialized_link->lady_lane_mask,
                };
            } else {
                mapped = lady_state_for_body_script_action(
                    selection->bank, selection->action);
            }
            if (mapped.has_value()) {
                state.lady_state = mapped->state;
                state.lady_lane_mask = mapped->lane_mask;

                // Reconstruct both CEm034+0x5070/+0x5190 controller starts,
                // including the asymmetric state55..82 cases.
                const auto starts = lady_body_state_scripts(mapped->state);
                state.lady_lane_mask = 0U;
                for (std::uint8_t lane = 0U; lane < 2U; ++lane) {
                    const auto& start = starts.lanes[lane];
                    if (!start.valid) continue;
                    state.lady_lane_mask |=
                        static_cast<std::uint8_t>(1U << lane);
                    state.lady_lane_signals[lane] =
                        binding.script->signals(start.bank, start.action);
                }
            } else {
                state.lady_state.reset();
                state.lady_lane_mask = 0U;
            }
            if (state.lady_state.has_value()) {
                state.lady_entry_applied = false;
                state.lady_runtime_frame = -1.0F;
                if (!apply_motion_frame(session, 0.0F)) {
                    clear_motion(session);
                    report = {};
                    report.detail =
                        "MotionScript: Lady runtime bridge rejected first frame";
                    return report;
                }
            }
        }

        report.detail =
            "MotionScript slot" + std::to_string(binding.archive_slot) +
            " bank" + std::to_string(selection->bank) +
            "/action" + std::to_string(selection->action) +
            (binding.role == Session::MotionScriptRole::LadyBody &&
                     state.lady_state.has_value()
                 ? " state" + std::to_string(*state.lady_state) +
                       " lanes=" + std::to_string(state.lady_lane_mask)
                 : std::string{}) +
            " -> " + report.detail;
        if (binding.role == Session::MotionScriptRole::LadyBody &&
            !state.lady_state.has_value()) {
            report.detail +=
                "\nLady runtime state bridge: action parsed, state semantics "
                "not promoted for this bank/action.";
        }
        return report;
    } catch (...) {
        report = {};
        report.detail = "MotionScript: playback failed";
        return report;
    }
}

std::span<const Vec3> motion_rest_vertices(const Session* session) noexcept {
    if (!has_motion(session)) return {};
    return session->motion->source_vertices;
}

}  // namespace dmcresource::motion
