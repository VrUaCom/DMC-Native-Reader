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
    struct ActiveScriptTrack final {
        ScriptControllerId controller{};
        std::size_t bank{};
        std::size_t action{};
        std::size_t motion_index{};
        Session::MotionScriptRole role{Session::MotionScriptRole::Primary};
    };

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
    std::size_t script_controller{std::numeric_limits<std::size_t>::max()};
    std::size_t script_motion_index{std::numeric_limits<std::size_t>::max()};
    std::uint64_t next_actor_instance{1U};
    std::array<std::uint64_t, 6> active_actor_instances{};
    std::vector<ActiveScriptTrack> script_tracks;
};

namespace {

struct ScriptMotionSelection final {
    std::size_t bank{};
    std::size_t action{};
    std::uint16_t resource_id{};
};

struct ResolvedScriptTrack final {
    ScriptControllerId controller{};
    ScriptActionId requested{};
    std::size_t bank{};
    std::size_t action{};
    std::size_t motion_index{};
    Session::MotionScriptRole role{Session::MotionScriptRole::Primary};
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
    // em034_013's only resource group is the EXE-bound PAC slot 11.
    if (groups.empty() && is_em034(session) &&
        binding.role == Session::MotionScriptRole::LadyComponent0) {
        return std::nullopt;
    }
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

[[nodiscard]] std::optional<ResolvedScriptTrack> resolve_script_track(
    const Session& session,
    ScriptControllerId controller,
    ScriptActionId requested) {
    if (controller >= session.motion_scripts.size()) return std::nullopt;
    const auto& binding = session.motion_scripts[controller];
    if (binding.script == nullptr) return std::nullopt;

    if (requested.motion_index >= session.motion_library.size() &&
        session.motion != nullptr) {
        for (const auto& active : session.motion->script_tracks) {
            if (active.controller != controller ||
                (requested.bank != std::numeric_limits<std::size_t>::max() &&
                 active.bank != requested.bank) ||
                (requested.action != std::numeric_limits<std::size_t>::max() &&
                 active.action != requested.action)) {
                continue;
            }
            requested.motion_index = active.motion_index;
            break;
        }
    }

    auto accept = [&](std::size_t motion_index, std::size_t bank,
                      std::size_t action) -> std::optional<ResolvedScriptTrack> {
        if (motion_index >= session.motion_library.size()) return std::nullopt;
        if (requested.bank != std::numeric_limits<std::size_t>::max() &&
            requested.bank != bank) {
            return std::nullopt;
        }
        if (requested.action != std::numeric_limits<std::size_t>::max() &&
            requested.action != action) {
            return std::nullopt;
        }
        return ResolvedScriptTrack{
            controller, requested, bank, action, motion_index, binding.role};
    };

    // Materialized ScriptLinks are the resource authority: search all of
    // them before the structural fallback, otherwise an earlier MOT of
    // another controller's pack that shares the resource id (em034 body
    // slot6 id 430 vs component slot11 id 430) would be picked first.
    for (std::size_t candidate = 0U;
         candidate < session.motion_library.size(); ++candidate) {
        if (requested.motion_index < session.motion_library.size() &&
            candidate != requested.motion_index) {
            continue;
        }
        const auto& motion = session.motion_library[candidate];
        for (const auto& link : motion.script_links) {
            if (link.script_index != controller ||
                (requested.bank != std::numeric_limits<std::size_t>::max() &&
                 link.bank != requested.bank) ||
                (requested.action != std::numeric_limits<std::size_t>::max() &&
                 link.action != requested.action)) {
                continue;
            }
            if (auto resolved = accept(candidate, link.bank, link.action);
                resolved.has_value()) {
                return resolved;
            }
        }
    }

    for (std::size_t candidate = 0U;
         candidate < session.motion_library.size(); ++candidate) {
        if (requested.motion_index < session.motion_library.size() &&
            candidate != requested.motion_index) {
            continue;
        }
        const auto& motion = session.motion_library[candidate];
        if (auto selection = pick_script_motion(session, binding, motion);
            selection.has_value()) {
            if (auto resolved = accept(
                    candidate, selection->bank, selection->action);
                resolved.has_value()) {
                return resolved;
            }
        }
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<ScriptControllerId> lady_component_controller(
    const Session& session) noexcept {
    for (std::size_t index = 0U; index < session.motion_scripts.size(); ++index) {
        if (session.motion_scripts[index].role ==
            Session::MotionScriptRole::LadyComponent0) {
            return index;
        }
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<LadyBodyLaneAction> lady_component_pair(
    const ResolvedScriptTrack& body) noexcept {
    // The component0 controller runs only where the CEm034 entry dispatcher
    // starts it and clears +0x4020 (lady_component_action_for_state).
    // Bank4/actions 3..5 (states 0x56..0x58) keep Kalina in the hands through
    // the CCnsMatrix (0x1401713F0(0, 1), +0x4020 = 1): no component track.
    if (body.role != Session::MotionScriptRole::LadyBody) return std::nullopt;
    const auto mapped = lady_state_for_body_script_action(body.bank, body.action);
    if (!mapped.has_value()) return std::nullopt;
    return lady_component_action_for_state(mapped->state);
}

[[nodiscard]] LadyComponentBinding* component0_binding(Session* session) noexcept {
    if (session == nullptr) return nullptr;
    for (auto& binding : session->lady_component_bindings) {
        if (binding.component == 0U) return &binding;
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

[[nodiscard]] Matrix4 without_translation(Matrix4 m) noexcept {
    // 0x14016F610 and 0x14016CBC8 copy the slot20 world and overwrite its
    // row 3 with (0,0,0,1) from .rdata 0x14035D5B0 before transforming a
    // (x,y,z,1) vector, so only the rotation rows take part.
    m.values[12] = 0.0F;
    m.values[13] = 0.0F;
    m.values[14] = 0.0F;
    m.values[15] = 1.0F;
    return m;
}

[[nodiscard]] Matrix4 align_z_matrix(Vec3 direction, Vec3 position) noexcept {
    // 0x14032FD90(out, dir, ref (0,1,0,1) from 0x1404C6440):
    // row0 = normalize(up x direction)
    // row1 = normalize(direction x row0)
    // row2 = normalize(direction)
    direction = normalize3(direction, {1.0F, 0.0F, 0.0F});
    const Vec3 up{0.0F, 1.0F, 0.0F};
    const Vec3 row0 = robust_cross(up, direction, true);
    const Vec3 row1 = normalize3(cross3(direction, row0), {0.0F, 1.0F, 0.0F});
    Matrix4 out;
    out.values = {
        row0.x, row0.y, row0.z, 0.0F,
        row1.x, row1.y, row1.z, 0.0F,
        direction.x, direction.y, direction.z, 0.0F,
        position.x, position.y, position.z, 1.0F,
    };
    return out;
}

// CEm034Shl02 flight table 0x14057B4E0 loaded by 0x140244940 and the fixed
// init offset .rdata 0x14057BB20.
constexpr float kShl02Speed = 30.0F;          // shell+0x160 per tick
constexpr float kShl02Lifetime = 120.0F;      // shell+0x17C
constexpr float kShl02RetargetInterval = 10.0F;  // shell+0x180/+0x184
constexpr Vec3 kShl02InitOffset{18.6F, 0.0F, 12.0F};
// State 2 (0x140173800) arms shell+0xD68 = 3.0 and enters state 3 once the
// countdown is negative (3->2->1->0->-1); the next update retires.
constexpr float kShl02ExplodeTicks = 4.0F;

struct Shl02Spawn final {
    Vec3 origin{};
    Vec3 direction{};  // unit, shell+0x140 after 0x140330390
};

[[nodiscard]] Shl02Spawn shl02_spawn(const Matrix4& slot20_node0) noexcept {
    // CEm034 0x140169937: dir = 0x14016F610 = (1,0,0,1) * slot20 world with
    // its translation removed, i.e. the slot20 X axis. The factory stores
    // pos = slot20 translation; init 0x1401738F0 adds (18.6,0,12) in world
    // axes (the constants are not rotated).
    const auto d4 = transform_row4(
        {1.0F, 0.0F, 0.0F, 1.0F}, without_translation(slot20_node0));
    Shl02Spawn out;
    out.direction = normalize3({d4.x, d4.y, d4.z}, {1.0F, 0.0F, 0.0F});
    out.origin = {
        slot20_node0.values[12] + kShl02InitOffset.x,
        slot20_node0.values[13] + kShl02InitOffset.y,
        slot20_node0.values[14] + kShl02InitOffset.z,
    };
    return out;
}

[[nodiscard]] Matrix4 shl02_world_at(const Session::LadyDynamicVisual& visual,
                                     float age) noexcept {
    // State 1 (0x140173C60 -> CShell::move 0x140244870, flags 3):
    // dir = normalize(dir) * 30; pos += dir * dt; lifetime -= dt. Until the
    // first retarget (timer 10 -> 0) the path needs no target. Afterwards the
    // EXE steers toward the player joint (0x140244B50, max turn u16 1200);
    // the standalone Reader has no player, so the direction is held and the
    // update carries requires_gameplay_world_context.
    const float flight = std::clamp(age, 0.0F, kShl02Lifetime);
    const Vec3 position{
        visual.origin.x + visual.velocity.x * kShl02Speed * flight,
        visual.origin.y + visual.velocity.y * kShl02Speed * flight,
        visual.origin.z + visual.velocity.z * kShl02Speed * flight,
    };
    return align_z_matrix(visual.velocity, position);
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
        {87.80000305F, 0.0F, 4.28000021F, 1.0F}, without_translation(*node0));
    const Vec3 spawn{
        offset.x + node1->values[12],
        offset.y + node1->values[13],
        offset.z + node1->values[14],
    };

    // Direction/velocity domain = 50 * ((1,0,0,1) * node0World).
    const auto d4 = transform_row4(
        {1.0F, 0.0F, 0.0F, 1.0F}, without_translation(*node0));
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
        visual.origin = {};
        visual.shell_state = 0U;
        visual.explode_emitted = false;
        visual.world = Matrix4{};
        visual.effect_parent_world = Matrix4{};
    }
}

void spawn_lady_dynamic_visual(
    Session* session, std::int8_t actor, float event_frame) noexcept {
    if (session == nullptr || actor < 0) return;

    if (actor == 2) {
        const auto node0 = lady_component_node_world(*session, 0U, 0U);
        if (!node0.has_value()) return;
        const auto spawn = shl02_spawn(*node0);
        const Matrix4 effect_parent_world =
            shl02_effect_parent_matrix(*node0);
        for (auto& visual : session->lady_dynamic_visuals) {
            if (visual.actor != 2U) continue;
            visual.origin = spawn.origin;
            visual.velocity = spawn.direction;
            visual.world = shl02_world_at(visual, 0.0F);
            visual.effect_parent_world = effect_parent_world;
            visual.shell_state = 1U;
            visual.explode_emitted = false;
            visual.spawn_frame = event_frame;
            visual.last_update_frame = event_frame;
            // Flight ends when shell+0x17C <= 0 (0x140244810 -> -1): state 2
            // on the next update, then kShl02ExplodeTicks to the retire.
            visual.retire_frame =
                event_frame + kShl02Lifetime + 1.0F + kShl02ExplodeTicks;
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
            visual.effect_parent_world = spawn->world;
            visual.velocity = spawn->velocity;
            visual.spawn_frame = event_frame;
            visual.last_update_frame = event_frame;
            visual.retire_frame = -1.0F;
            visual.active = true;
        }
    }
}

[[nodiscard]] std::optional<Matrix4> lady_dynamic_actor_effect_parent_world(
    const Session& session, std::uint8_t actor) noexcept {
    for (const auto& visual : session.lady_dynamic_visuals) {
        if (visual.actor == actor && visual.active) {
            return visual.world;
        }
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<float> lady_dynamic_actor_spawn_frame(
    const Session& session, std::uint8_t actor) noexcept {
    for (const auto& visual : session.lady_dynamic_visuals) {
        if (visual.actor == actor && visual.active &&
            visual.spawn_frame >= 0.0F) {
            return visual.spawn_frame;
        }
    }
    return std::nullopt;
}

void emit_lady_actor_event(Session* session,
                           MotionState& state,
                           std::int8_t actor,
                           std::uint16_t actor_state,
                           std::uint8_t lane,
                           std::uint8_t channel,
                           std::uint8_t value,
                           float frame) {
    if (session == nullptr || actor < 0 || actor >= 6 ||
        session->effect_runtime == nullptr) {
        return;
    }
    const auto actor_index = static_cast<std::uint8_t>(actor);
    const auto instance = state.next_actor_instance++;
    state.active_actor_instances[actor_index] = instance;

    DynamicActorEvent event;
    event.kind = DynamicActorEventKind::Spawn;
    event.actor = actor_index;
    event.actor_state = actor_state;
    event.lane = lane;
    event.channel = channel;
    event.signal_value = value;
    event.actor_instance = instance;
    event.script_frame = frame;
    event.evidence = EvidenceStatus::EXE_AND_CORPUS_CONFIRMED;
    for (auto& visual : session->lady_dynamic_visuals) {
        if (visual.actor != actor_index || !visual.active) continue;
        // Live shell matrix (V377 follows it through effect+0xC0) and, as a
        // separate copied matrix, the spawn matrix CEm034 hands to
        // 0x1402E7A90 (Shl02: normalized slot20 world for V423).
        event.world = visual.world;
        event.world_authoritative = actor_index == 2U || actor_index == 3U;
        event.spawn_matrix = actor_index == 2U ? visual.effect_parent_world
                                               : visual.world;
        event.spawn_matrix_authoritative = event.world_authoritative;
        visual.spawn_signal = {lane, channel, value};
        break;
    }
    session->effect_runtime->apply_actor_event(event);
}

// CEm034Shl02 state 2 (0x140173800) spawns V543 with a copy of the shell
// matrix; 0x1402E7CA0 adds 2.0 (.rdata 0x14035D570) to its translation y.
void emit_shl02_explode_event(Session* session,
                              const MotionState& state,
                              Session::LadyDynamicVisual& visual) {
    if (session == nullptr || session->effect_runtime == nullptr ||
        visual.actor != 2U || state.active_actor_instances[2U] == 0U) {
        return;
    }
    DynamicActorEvent event;
    event.kind = DynamicActorEventKind::Spawn;
    event.actor = 2U;
    event.actor_phase = 2U;
    event.lane = visual.spawn_signal[0];
    event.channel = visual.spawn_signal[1];
    event.signal_value = visual.spawn_signal[2];
    event.actor_instance = state.active_actor_instances[2U];
    event.script_frame = visual.spawn_frame + kShl02Lifetime + 1.0F;
    event.world = shl02_world_at(visual, kShl02Lifetime);
    event.world_authoritative = true;
    event.spawn_matrix = event.world;
    event.spawn_matrix.values[13] += 2.0F;
    event.spawn_matrix_authoritative = true;
    // The standalone flight reached its lifetime; a gameplay collision or
    // the 100-unit player proximity (0x140244810) would end it earlier.
    event.requires_gameplay_world_context = true;
    event.evidence = EvidenceStatus::EXE_AND_CORPUS_CONFIRMED;
    session->effect_runtime->apply_actor_event(event);
}

void sync_lady_actor_effects(Session* session,
                             MotionState& state,
                             float frame) {
    if (session == nullptr || session->effect_runtime == nullptr) return;
    for (std::uint8_t actor = 0U; actor < state.active_actor_instances.size(); ++actor) {
        const auto instance = state.active_actor_instances[actor];
        if (instance == 0U) continue;
        if (actor == 2U) {
            // Also reached by a seek past the flight: the explode spawn
            // precedes the retire of the same shell instance.
            for (auto& visual : session->lady_dynamic_visuals) {
                if (visual.actor == 2U && visual.spawn_frame >= 0.0F &&
                    visual.shell_state == 2U && !visual.explode_emitted) {
                    visual.explode_emitted = true;
                    emit_shl02_explode_event(session, state, visual);
                }
            }
        }
        const auto world = lady_dynamic_actor_effect_parent_world(
            *session, actor);
        if (world.has_value()) {
            // The spawn event already carries this exact matrix. Do not emit
            // a synthetic same-frame update; a seek/replay consumer should
            // observe one canonical spawn followed by later updates.
            const auto spawn_frame =
                lady_dynamic_actor_spawn_frame(*session, actor);
            if (spawn_frame.has_value() && frame <= *spawn_frame) continue;
            DynamicActorEvent event;
            event.kind = DynamicActorEventKind::Update;
            event.actor = actor;
            event.actor_instance = instance;
            event.script_frame = frame;
            event.world = *world;
            event.world_authoritative = actor == 2U || actor == 3U;
            // From the first retarget on, the shell path depends on the
            // player target the standalone Reader does not have.
            event.requires_gameplay_world_context =
                actor == 2U && spawn_frame.has_value() &&
                frame - *spawn_frame >= kShl02RetargetInterval;
            event.evidence = EvidenceStatus::EXE_AND_CORPUS_CONFIRMED;
            session->effect_runtime->apply_actor_event(event);
        } else if (actor == 2U || actor == 3U) {
            DynamicActorEvent event;
            event.kind = DynamicActorEventKind::Retire;
            event.actor = actor;
            event.actor_instance = instance;
            event.script_frame = frame;
            event.evidence = EvidenceStatus::EXE_AND_CORPUS_CONFIRMED;
            session->effect_runtime->apply_actor_event(event);
            state.active_actor_instances[actor] = 0U;
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
            const float age = frame - visual.spawn_frame;
            visual.world = shl02_world_at(visual, age);
            if (age >= kShl02Lifetime + 1.0F) visual.shell_state = 2U;
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
                                             float frame) {
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
        state.active_actor_instances.fill(0U);
        state.next_actor_instance = 1U;
        if (session->effect_runtime != nullptr) session->effect_runtime->reset();
        const auto entry = apply_lady_state_entry(session, *state.lady_state);
        state.lady_entry_applied = true;
        state.lady_runtime_frame = -1.0F;
        if (entry.dynamic_actor >= 0) {
            state.last_dynamic_actor = entry.dynamic_actor;
            spawn_lady_dynamic_visual(session, entry.dynamic_actor, 0.0F);
            emit_lady_actor_event(
                session, state, entry.dynamic_actor, *state.lady_state,
                0xFFU, 0xFFU, 0xFFU, 0.0F);
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
                    spawn_lady_dynamic_visual(
                        session, applied.dynamic_actor,
                        std::max(signal.after_frame, 0.0F));
                    emit_lady_actor_event(
                        session, state, applied.dynamic_actor, *state.lady_state,
                        lane, channel, value,
                        std::max(signal.after_frame, 0.0F));
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

    // The component0 domain follows the EXE: the state entry clears +0x4020
    // for the paired states, and a lane0 signal (e.g. state 0x7B) may
    // re-enable the hand constraint mid-action. It is not forced back to the
    // independent domain here.

    advance_lady_dynamic_visuals(session, frame);
    sync_lady_actor_effects(session, state, frame);
    state.lady_runtime_frame = frame;
    return true;
}

}  // namespace

Matrix4 shl02_shell_world(const Matrix4& slot20_node0, float age) noexcept {
    const auto spawn = shl02_spawn(slot20_node0);
    Session::LadyDynamicVisual visual;
    visual.origin = spawn.origin;
    visual.velocity = spawn.direction;
    return shl02_world_at(visual, std::isfinite(age) ? age : 0.0F);
}

Matrix4 shl02_effect_parent_matrix(const Matrix4& slot20_node0) noexcept {
    Matrix4 out = slot20_node0;
    // 0x1402e7ab0 copies the selected matrix, then normalizes row0..row2
    // through 0x140330390. That helper normalizes xyz; the fourth lane is
    // not written back. Translation row3 is preserved verbatim.
    for (std::size_t row = 0U; row < 3U; ++row) {
        const std::size_t base = row * 4U;
        const float x = slot20_node0.values[base + 0U];
        const float y = slot20_node0.values[base + 1U];
        const float z = slot20_node0.values[base + 2U];
        const float length_sq = x * x + y * y + z * z;
        if (!(length_sq > 1.0e-15F) || !std::isfinite(length_sq)) {
            continue;
        }
        const float inverse_length = 1.0F / std::sqrt(length_sq);
        out.values[base + 0U] = x * inverse_length;
        out.values[base + 1U] = y * inverse_length;
        out.values[base + 2U] = z * inverse_length;
    }
    return out;
}

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
        // Raw MOT is deliberately script-free. Weapon/equipment timelines are
        // installed by load_scripted_motion after the controller/action has
        // been selected; a raw MOT may only evaluate its skeleton channels.

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

[[nodiscard]] MotionLoadReport load_synchronized_script_tracks(
    Session* session,
    std::span<const ResolvedScriptTrack> requested_tracks) noexcept {
    MotionLoadReport report;
    if (session == nullptr || requested_tracks.empty()) {
        report.detail = "MotionScript: no synchronized tracks";
        return report;
    }

    try {
        clear_motion(session);
        auto state = std::make_shared<MotionState>();
        state->name = "synchronized-script-track-set";
        state->script_driven = true;

        std::vector<ResolvedScriptTrack> tracks;
        tracks.reserve(requested_tracks.size());
        std::vector<std::string> deferred;

        // Enter independent component control before binding its own MOT. A
        // body state entry may later rebuild the placement; the final runtime
        // pass reasserts this domain while the component track is active.
        for (const auto& track : requested_tracks) {
            if (track.role != Session::MotionScriptRole::LadyComponent0) {
                tracks.push_back(track);
                continue;
            }
            auto* component = component0_binding(session);
            if (component == nullptr) {
                deferred.push_back("component0 binding unavailable");
                continue;
            }
            const auto initial =
                track.bank == 4U &&
                        (track.action == 41U || track.action == 42U)
                    ? LadyPlacementPreset::ActiveDeployed
                    : LadyPlacementPreset::BodyStowed;
            if (!set_lady_component_preset(session, *component, initial) ||
                !set_lady_component_control_domain(
                    session, *component,
                    LadyControlDomain::IndependentMotionScript)) {
                deferred.push_back("component0 independent domain unavailable");
                continue;
            }
            tracks.push_back(track);
        }

        if (tracks.empty()) {
            report.detail = "MotionScript: all synchronized tracks deferred";
            report.deferred_tracks = requested_tracks.size();
            reset_lady_runtime(session);
            return report;
        }

        const std::size_t part_count = session->composite_parts.empty()
            ? 1U
            : session->composite_parts.size();
        std::vector<int> owner(part_count, -1);
        std::vector<bool> track_bound(tracks.size(), false);

        const auto body_part = [&]() -> std::optional<std::size_t> {
            if (session->composite_parts.empty()) return 0U;
            if (const auto* component = component0_binding(session);
                component != nullptr &&
                component->host_part < session->composite_parts.size()) {
                return component->host_part;
            }
            return std::nullopt;
        };

        const auto component_part = [&]() -> std::optional<std::size_t> {
            if (session->composite_parts.empty()) return 0U;
            if (const auto* component = component0_binding(session);
                component != nullptr &&
                component->part < session->composite_parts.size()) {
                return component->part;
            }
            return std::nullopt;
        };

        // Reserve explicit Lady body/component ownership first. Primary or
        // future profile tracks may still fan out to every compatible
        // source-space part, matching the legacy single-MOT behavior.
        for (std::size_t index = 0U; index < tracks.size(); ++index) {
            const auto& track = tracks[index];
            std::optional<std::size_t> explicit_part;
            if (track.role == Session::MotionScriptRole::LadyBody) {
                explicit_part = body_part();
            } else if (track.role == Session::MotionScriptRole::LadyComponent0) {
                explicit_part = component_part();
            }
            if (!explicit_part.has_value()) continue;
            if (*explicit_part >= owner.size() || owner[*explicit_part] >= 0) {
                deferred.push_back("duplicate synchronized part owner");
                continue;
            }
            owner[*explicit_part] = static_cast<int>(index);
        }

        for (std::size_t index = 0U; index < tracks.size(); ++index) {
            const auto& track = tracks[index];
            if (track.role == Session::MotionScriptRole::LadyBody ||
                track.role == Session::MotionScriptRole::LadyComponent0) {
                continue;
            }
            for (std::size_t part = 0U; part < owner.size(); ++part) {
                if (owner[part] >= 0 ||
                    (!session->composite_parts.empty() &&
                     session->composite_parts[part].placement.mode ==
                         CompositePlacementMode::HostJointSkeleton)) {
                    continue;
                }
                owner[part] = static_cast<int>(index);
            }
        }

        std::string reasons;
        std::size_t vertex_cursor = 0U;
        std::size_t node_cursor = 0U;
        for (std::size_t part_index = 0U; part_index < part_count; ++part_index) {
            const RenderScene& scene = session->composite_parts.empty()
                ? session->scene
                : session->composite_parts[part_index].scene;
            const std::size_t vertices = scene_vertex_count(scene);
            const std::size_t nodes = scene.nodes.size();

            if (owner[part_index] < 0) {
                ++state->static_parts;
                vertex_cursor += vertices;
                node_cursor += nodes;
                continue;
            }

            const auto track_index = static_cast<std::size_t>(owner[part_index]);
            const auto& track = tracks[track_index];
            const auto& payload = session->motion_library[track.motion_index];
            const CompositePlacement* placement = session->composite_parts.empty()
                ? nullptr
                : &session->composite_parts[part_index].placement;
            std::string reason;
            auto part_motion = bind_part(
                scene, vertex_cursor, node_cursor, placement,
                std::span<const std::byte>{
                    reinterpret_cast<const std::byte*>(payload.bytes.data()),
                    payload.bytes.size()},
                &reason);
            if (part_motion.has_value()) {
                state->parts.push_back(std::move(*part_motion));
                track_bound[track_index] = true;
            } else {
                ++state->static_parts;
                if (!reasons.empty()) reasons += "; ";
                reasons += "track" + std::to_string(track_index) + ": " + reason;
            }
            vertex_cursor += vertices;
            node_cursor += nodes;
        }

        for (std::size_t index = 0U; index < tracks.size(); ++index) {
            if (!track_bound[index]) {
                deferred.push_back(
                    "track" + std::to_string(index) + " has no compatible part");
            }
        }

        if (vertex_cursor != session->render_mesh.vertices.size() ||
            node_cursor != session->scene.nodes.size()) {
            report.detail = "MotionScript: synchronized projection mismatch";
            reset_lady_runtime(session);
            return report;
        }
        if (state->parts.empty()) {
            report.detail = "MotionScript: synchronized tracks drive no part";
            report.deferred_tracks = requested_tracks.size();
            reset_lady_runtime(session);
            return report;
        }

        const std::size_t primary_index = [&]() {
            for (std::size_t index = 0U; index < tracks.size(); ++index) {
                if (tracks[index].role == Session::MotionScriptRole::LadyBody ||
                    tracks[index].role == Session::MotionScriptRole::Primary) {
                    return index;
                }
            }
            return std::size_t{0U};
        }();
        const auto& primary = tracks[primary_index];
        const auto& primary_binding = session->motion_scripts[primary.controller];
        const auto& primary_payload = session->motion_library[primary.motion_index];
        state->script_role = primary.role;
        state->script_slot = primary_binding.archive_slot;
        state->script_controller = primary.controller;
        state->script_motion_index = primary.motion_index;
        state->script_bank = primary.bank;
        state->script_action = primary.action;
        state->script_signals = primary_binding.script->signals(
            primary.bank, primary.action);
        if (primary_payload.bank >= 0 && primary_payload.index >= 0) {
            state->weapon_keys = primary_binding.script->weapon_states_for_motion(
                static_cast<std::size_t>(primary_payload.bank),
                static_cast<std::size_t>(primary_payload.index));
        }

        for (std::size_t index = 0U; index < tracks.size(); ++index) {
            if (!track_bound[index]) continue;
            state->script_tracks.push_back({
                tracks[index].controller,
                tracks[index].bank,
                tracks[index].action,
                tracks[index].motion_index,
                tracks[index].role});
        }

        if (primary.role == Session::MotionScriptRole::LadyBody) {
            std::optional<LadyBodyScriptState> mapped;
            for (const auto& link : primary_payload.script_links) {
                if (link.script_index == primary.controller &&
                    link.bank == primary.bank && link.action == primary.action &&
                    link.lady_state >= 0) {
                    mapped = LadyBodyScriptState{
                        static_cast<std::uint16_t>(link.lady_state),
                        link.lady_lane_mask};
                    break;
                }
            }
            if (!mapped.has_value()) {
                mapped = lady_state_for_body_script_action(
                    primary.bank, primary.action);
            }
            if (mapped.has_value()) {
                state->lady_state = mapped->state;
                state->lady_lane_mask = mapped->lane_mask;
                const auto starts = lady_body_state_scripts(mapped->state);
                state->lady_lane_mask = 0U;
                for (std::uint8_t lane = 0U; lane < 2U; ++lane) {
                    const auto& start = starts.lanes[lane];
                    if (!start.valid) continue;
                    state->lady_lane_mask |=
                        static_cast<std::uint8_t>(1U << lane);
                    state->lady_lane_signals[lane] =
                        primary_binding.script->signals(start.bank, start.action);
                }
                state->lady_entry_applied = false;
                state->lady_runtime_frame = -1.0F;
            }
        }

        state->source_vertices = session->render_mesh.vertices;
        state->source_node_world.reserve(session->scene.nodes.size());
        for (const auto& node : session->scene.nodes) {
            state->source_node_world.push_back(node.world);
        }
        state->source_overlay = session->hierarchy_overlay;
        state->end_frame = 0.0F;
        state->loop_start_frame = 0.0F;
        for (const auto& part : state->parts) {
            state->end_frame = std::max(state->end_frame, part.clip.end_frame());
            state->loop_start_frame = std::max(
                state->loop_start_frame, part.clip.loop_start_frame());
        }

        session->motion = std::move(state);
        if (session->script_effect_bridge.prepare != nullptr &&
            !session->script_effect_bridge.prepare(session)) {
            session->script_effect_bindings.clear();
        }
        (void)ensure_effect_runtime(session);

        report.ok = true;
        report.animated_parts = session->motion->parts.size();
        report.static_parts = session->motion->static_parts;
        report.synchronized_tracks = session->motion->script_tracks.size();
        report.deferred_tracks = requested_tracks.size() -
            std::min(requested_tracks.size(), report.synchronized_tracks);
        report.end_frame = session->motion->end_frame;
        report.detail = "MotionScript synchronized tracks=" +
            std::to_string(report.synchronized_tracks) +
            " deferred=" + std::to_string(report.deferred_tracks) +
            " animatedParts=" + std::to_string(report.animated_parts) +
            " staticParts=" + std::to_string(report.static_parts);
        if (!deferred.empty()) {
            report.detail += "\nDeferred: ";
            for (std::size_t index = 0U; index < deferred.size(); ++index) {
                if (index != 0U) report.detail += "; ";
                report.detail += deferred[index];
            }
        }
        if (!reasons.empty()) report.detail += "\nStatic parts: " + reasons;

        if (!apply_motion_frame(session, 0.0F)) {
            clear_motion(session);
            report = {};
            report.detail = "MotionScript: synchronized first frame failed";
        }
        return report;
    } catch (const std::bad_alloc&) {
        report = {};
        report.detail = "MotionScript: synchronized allocation failed";
        return report;
    } catch (...) {
        report = {};
        report.detail = "MotionScript: synchronized playback failed";
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
        const bool rewinding = state.last_frame >= 0.0F && frame < state.last_frame;
        if (rewinding && state.script_role != Session::MotionScriptRole::LadyBody) {
            if (session->script_effect_bridge.reset != nullptr) {
                session->script_effect_bridge.reset(session);
            }
            if (session->effect_runtime != nullptr) {
                session->effect_runtime->reset();
            }
        }
        if (session->effect_runtime != nullptr) session->effect_runtime->begin_step();
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
        if (state.script_driven &&
            state.script_role != Session::MotionScriptRole::LadyBody &&
            session->script_effect_bridge.step != nullptr) {
            session->script_effect_bridge.step(session, frame);
        }
        if (session->effect_runtime != nullptr) {
            session->effect_runtime->advance(frame);
        }
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
    if (session == nullptr) return;
    if (session->motion != nullptr && session->motion->script_driven &&
        session->script_effect_bridge.reset != nullptr) {
        session->script_effect_bridge.reset(session);
    }
    if (session->effect_runtime != nullptr) session->effect_runtime->reset();
    if (session->motion == nullptr) return;
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
        // An em034 body state that starts the component0 controller has two
        // independent controller tracks: the body MOT and component0 MOT. Keep the
        // legacy public load entry point compatible with that contract so a
        // UI/JNI Script Play call cannot silently drop the component track.
        const auto binding = session->motion_scripts[script_index];
        if (binding.role == Session::MotionScriptRole::LadyBody) {
            const auto body = resolve_script_track(
                *session,
                script_index,
                ScriptActionId{
                    std::numeric_limits<std::size_t>::max(),
                    std::numeric_limits<std::size_t>::max(),
                    motion_index});
            const auto pair = body.has_value()
                ? lady_component_pair(*body)
                : std::optional<LadyBodyLaneAction>{};
            if (pair.has_value()) {
                if (const auto component_controller =
                        lady_component_controller(*session);
                    component_controller.has_value()) {
                    const auto component = resolve_script_track(
                        *session,
                        *component_controller,
                        ScriptActionId{
                            pair->bank,
                            pair->action,
                            std::numeric_limits<std::size_t>::max()});
                    if (component.has_value()) {
                        const std::array<ResolvedScriptTrack, 2> pair{
                            *body, *component};
                        return load_synchronized_script_tracks(
                            session, std::span<const ResolvedScriptTrack>{
                                          pair.data(), pair.size()});
                    }
                }
            }
        }

        clear_motion(session);
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
        state.script_controller = script_index;
        state.script_motion_index = motion_index;
        state.script_bank = selection->bank;
        state.script_action = selection->action;
        state.script_signals =
            binding.script->signals(selection->bank, selection->action);
        if (payload.bank >= 0 && payload.index >= 0) {
            // The action controller, not the raw MOT loader, owns this
            // script-side equipment timeline.
            state.weapon_keys = binding.script->weapon_states_for_motion(
                static_cast<std::size_t>(payload.bank),
                static_cast<std::size_t>(payload.index));
        }

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
            }
        }

        // Every profile uses one preparation/installation path. The callback
        // only registers evidence-backed data; the shared runtime remains the
        // sole owner of effect instances and lifecycle state.
        if (session->script_effect_bridge.prepare != nullptr &&
            !session->script_effect_bridge.prepare(session)) {
            session->script_effect_bindings.clear();
        }
        (void)ensure_effect_runtime(session);

        const bool needs_script_frame_zero =
            state.lady_state.has_value() ||
            session->effect_runtime != nullptr ||
            session->script_effect_bridge.step != nullptr;
        if (needs_script_frame_zero && !apply_motion_frame(session, 0.0F)) {
            clear_motion(session);
            report = {};
            report.detail =
                "MotionScript: profile runtime bridge rejected first frame";
            return report;
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

RuntimeStepResult run_script_frame(Session* session,
                                   ScriptControllerId controller,
                                   ScriptActionId action,
                                   float frame) noexcept {
    std::array<ScriptTrackAction, 2> tracks{
        ScriptTrackAction{controller, action},
        ScriptTrackAction{},
    };
    std::size_t track_count = 1U;

    // An em034 body state may start the component0 controller. Expose
    // that pairing through the existing single-controller API so Android/JNI
    // callers get the same synchronized path as native replay callers.
    if (session != nullptr) {
        const auto body = resolve_script_track(*session, controller, action);
        if (const auto pair = body.has_value()
                ? lady_component_pair(*body)
                : std::optional<LadyBodyLaneAction>{};
            pair.has_value()) {
            if (const auto component_controller = lady_component_controller(*session);
                component_controller.has_value()) {
                tracks[track_count++] = ScriptTrackAction{
                    *component_controller,
                    ScriptActionId{
                        pair->bank,
                        pair->action,
                        std::numeric_limits<std::size_t>::max(),
                    }};
            }
        }
    }
    return run_synchronized_script_frame(
        session, std::span<const ScriptTrackAction>{tracks.data(), track_count},
        frame);
}

RuntimeStepResult run_synchronized_script_frame(
    Session* session,
    std::span<const ScriptTrackAction> requested_tracks,
    float frame) noexcept {
    RuntimeStepResult result;
    try {
        if (session == nullptr || !std::isfinite(frame) ||
            requested_tracks.empty()) {
            return result;
        }

        std::vector<ResolvedScriptTrack> resolved;
        resolved.reserve(requested_tracks.size());
        for (const auto& requested : requested_tracks) {
            if (const auto track = resolve_script_track(
                    *session, requested.controller, requested.action);
                track.has_value()) {
                resolved.push_back(*track);
            } else {
                ++result.deferred_tracks;
            }
        }
        if (resolved.empty()) return result;

        const auto same_track = [](const MotionState::ActiveScriptTrack& active,
                                   const ResolvedScriptTrack& requested) {
            return active.controller == requested.controller &&
                   active.bank == requested.bank &&
                   active.action == requested.action &&
                   active.motion_index == requested.motion_index &&
                   active.role == requested.role;
        };
        const auto is_bound = [&]() {
            if (session->motion == nullptr ||
                !session->motion->script_driven ||
                session->motion->script_tracks.size() != resolved.size()) {
                return false;
            }
            for (const auto& requested : resolved) {
                const auto active = std::find_if(
                    session->motion->script_tracks.begin(),
                    session->motion->script_tracks.end(),
                    [&requested, &same_track](
                        const MotionState::ActiveScriptTrack& candidate) {
                        return same_track(candidate, requested);
                    });
                if (active == session->motion->script_tracks.end()) return false;
            }
            return true;
        };

        // Capture before the optional load as well: the first synchronized
        // Script Play call may enter a new CEm034 placement/control domain.
        const auto capture_components = [](const Session& source) {
            std::vector<ComponentTransition> snapshot;
            snapshot.reserve(source.lady_component_bindings.size());
            for (const auto& binding : source.lady_component_bindings) {
                snapshot.push_back({
                    binding.component,
                    static_cast<std::uint8_t>(binding.preset),
                    static_cast<std::uint8_t>(binding.control_domain),
                    binding.runtime_uniform_scale,
                });
            }
            return snapshot;
        };
        const auto before_components = capture_components(*session);

        const bool loaded = !is_bound();
        if (loaded) {
            const auto report = load_synchronized_script_tracks(
                session,
                std::span<const ResolvedScriptTrack>{
                    resolved.data(), resolved.size()});
            result.synchronized_tracks = report.synchronized_tracks;
            result.deferred_tracks += report.deferred_tracks;
            if (!report.ok) return result;
        }
        if (session->motion == nullptr || !session->motion->script_driven) {
            return result;
        }
        result.synchronized_tracks = session->motion->script_tracks.size();

        // The load path materializes frame 0 once. Preserve its spawn/event
        // stream when the caller asks for that same frame rather than clearing
        // it with an identical second evaluation.
        if (!(loaded && frame == 0.0F) &&
            !apply_motion_frame(session, frame)) {
            return result;
        }

        const auto after_components = capture_components(*session);
        for (const auto& after : after_components) {
            const auto before = std::find_if(
                before_components.begin(), before_components.end(),
                [&after](const ComponentTransition& candidate) {
                    return candidate.component == after.component;
                });
            if (before == before_components.end() ||
                before->placement != after.placement ||
                before->control_domain != after.control_domain ||
                before->uniform_scale != after.uniform_scale) {
                result.component_changes.push_back(after);
            }
        }

        if (session->effect_runtime != nullptr) {
            const auto actors = session->effect_runtime->actor_events();
            const auto effects = session->effect_runtime->effect_events();
            result.actor_events.assign(actors.begin(), actors.end());
            result.effect_events.assign(effects.begin(), effects.end());
        }
    } catch (...) {
        // The public runtime boundary is a no-throw API for the JNI/viewer
        // shells. A failed materialization returns an empty step instead of
        // terminating on vector growth or a malformed profile payload.
        return {};
    }
    return result;
}

std::span<const RuntimeEffectInstance> active_effect_instances(
    const Session* session) noexcept {
    if (session == nullptr || session->effect_runtime == nullptr) return {};
    return session->effect_runtime->active_instances();
}

std::span<const RuntimeEffectInstance> presentation_effect_instances(
    const Session* session) noexcept {
    if (session == nullptr || session->effect_runtime == nullptr) return {};
    return session->effect_runtime->presentation_instances();
}

std::span<const RuntimeEffectEvent> effect_events(
    const Session* session) noexcept {
    if (session == nullptr || session->effect_runtime == nullptr) return {};
    return session->effect_runtime->effect_events();
}

void set_effects_visible(Session* session, bool visible) noexcept {
    if (session == nullptr) return;
    session->effects_visible = visible;
    if (session->effect_runtime != nullptr) {
        session->effect_runtime->set_presentation_enabled(visible);
    }
}

bool effects_visible(const Session* session) noexcept {
    return session != nullptr && session->effects_visible;
}

std::span<const Vec3> motion_rest_vertices(const Session* session) noexcept {
    if (!has_motion(session)) return {};
    return session->motion->source_vertices;
}

}  // namespace dmcresource::motion
