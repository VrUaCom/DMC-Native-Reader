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
    std::vector<ScriptSignalKey> script_signals;
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

[[nodiscard]] std::optional<std::uint16_t> lady_group_for_pack(
    const Session& session,
    const Session::MotionScriptBinding& binding,
    const Session::MotionPayload& motion) noexcept {
    if (!is_em034(session) || motion.pack_slot < 0) return std::nullopt;

    if (binding.role == Session::MotionScriptRole::LadyComponent0) {
        // EXE_AND_CORPUS_CONFIRMED:
        // em034_013 bank4 -> top-level PAC slot11.
        return motion.pack_slot == 11 ? std::optional<std::uint16_t>{4U}
                                      : std::nullopt;
    }

    if (binding.role == Session::MotionScriptRole::LadyBody) {
        // CEm034 body motion packs are laid out in PAC order for script groups
        // 0..4: top-level slots2..6. The group6 resource remains external/
        // unresolved and is intentionally not fabricated here.
        if (motion.pack_slot >= 2 && motion.pack_slot <= 6) {
            return static_cast<std::uint16_t>(motion.pack_slot - 2);
        }
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<ScriptMotionSelection> pick_script_motion(
    const Session& session,
    const Session::MotionScriptBinding& binding,
    const Session::MotionPayload& motion) {
    if (binding.script == nullptr || motion.mot_slot < 0) return std::nullopt;

    std::vector<std::uint16_t> groups;
    if (const auto lady = lady_group_for_pack(session, binding, motion)) {
        groups.push_back(*lady);
    } else if (motion.pack_slot >= 0) {
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
        const auto entry = apply_lady_state_entry(session, *state.lady_state);
        if (!entry.recognized) return false;
        state.lady_entry_applied = true;
        state.lady_runtime_frame = -1.0F;
        if (entry.dynamic_actor >= 0) state.last_dynamic_actor = entry.dynamic_actor;
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
    for (const auto& signal : state.script_signals) {
        if (!(frame > signal.after_frame)) continue;
        if (!replay && signal.after_frame < state.lady_runtime_frame) continue;

        for (std::uint8_t lane = 0U; lane < 2U; ++lane) {
            for (std::uint8_t channel = 0U; channel < signal.channels.size(); ++channel) {
                const auto value = signal.channels[channel];
                if (value == 0U &&
                    !(*state.lady_state == 0x81U && channel == 1U)) {
                    continue;
                }
                const auto applied = apply_lady_signal(
                    session, *state.lady_state, lane, channel, value);
                if (applied.dynamic_actor >= 0) {
                    state.last_dynamic_actor = applied.dynamic_actor;
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
        return pick_script_motion(
                   *session,
                   session->motion_scripts[script_index],
                   session->motion_library[motion_index])
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
        const auto binding = session->motion_scripts[script_index];
        const auto payload = session->motion_library[motion_index];
        const auto selection = pick_script_motion(*session, binding, payload);
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
            state.lady_state =
                lady_state_for_script_action(selection->bank, selection->action);
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
