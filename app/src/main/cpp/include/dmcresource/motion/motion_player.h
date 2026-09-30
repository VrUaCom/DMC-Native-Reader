#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <string_view>

#include "dmcresource/motion/effect_runtime.h"

namespace dmcresource {
struct Session;
struct Vec3;
}

namespace dmcresource::motion {

struct MotionLoadReport final {
    bool ok{false};
    std::size_t animated_parts{};
    std::size_t static_parts{};
    float end_frame{};
    std::string detail;
};

// Bind one MOT to the live Session (single MOD or MOD composite). Every model
// part whose skeleton matches the MOT channel domain is animated; others keep
// their source pose and are reported. Nothing is written back to any file.
[[nodiscard]] MotionLoadReport load_motion(Session* session,
                                           std::string_view name,
                                           const std::uint8_t* bytes,
                                           std::size_t size) noexcept;

// Raw library MOT playback. This remains script-free, but may temporarily
// release a component from its host constraint when the MOT targets that
// component's own skeleton (CEm034 slot20 / PAC11).
[[nodiscard]] MotionLoadReport load_library_motion(Session* session,
                                                   std::size_t motion_index) noexcept;

// Pose the Session at `frame` (MOT timeline units, 60 per second in DMC3).
// Skins the render mesh through inverseRest * currentWorld and refreshes the
// skeleton overlay. Returns false when no motion is bound.
[[nodiscard]] bool apply_motion_frame(Session* session, float frame) noexcept;

// Restore the source (rest) pose and drop the bound motion.
void clear_motion(Session* session) noexcept;

[[nodiscard]] bool has_motion(const Session* session) noexcept;
[[nodiscard]] float motion_end_frame(const Session* session) noexcept;
[[nodiscard]] float motion_loop_start_frame(const Session* session) noexcept;

// Rest-pose vertices of the posed Session (empty when no motion is bound);
// used to keep camera framing stable during playback.
[[nodiscard]] std::span<const Vec3> motion_rest_vertices(const Session* session) noexcept;

// Whether a MOT can drive at least one skinned part of the session (the
// same binding load_motion uses). Host-joint parts (coats) do not count.
[[nodiscard]] bool motion_can_drive(const Session& session, std::span<const std::uint8_t> mot) noexcept;

// Retail 0x1402e7a90 mode=3 prepares the V423 parent by normalizing the
// first three rows of the selected CEm034 slot20 world matrix. The actor's
// render basis remains a separate transform.
[[nodiscard]] Matrix4 shl02_effect_parent_matrix(
    const Matrix4& slot20_node0) noexcept;

// MotionScript playback is intentionally separate from raw MOT playback.
// A PAC may retain multiple independent script controllers; each script button
// can address only MOTs referenced by that script's resource table.
[[nodiscard]] std::size_t motion_script_count(const Session* session) noexcept;
[[nodiscard]] std::uint32_t motion_script_slot(const Session* session,
                                               std::size_t script_index) noexcept;
[[nodiscard]] bool motion_script_can_play_motion(const Session* session,
                                                 std::size_t script_index,
                                                 std::size_t motion_index) noexcept;
[[nodiscard]] MotionLoadReport load_scripted_motion(Session* session,
                                                    std::size_t script_index,
                                                    std::size_t motion_index) noexcept;

using ScriptControllerId = std::size_t;

// Generic Script Play frame boundary. `motion_index` is optional: when left at
// max, the bound script action or canonical script link/group map resolves the
// MOT. Bank/action can be supplied when the caller already has script identity.
struct ScriptActionId final {
    std::size_t bank{std::numeric_limits<std::size_t>::max()};
    std::size_t action{std::numeric_limits<std::size_t>::max()};
    std::size_t motion_index{std::numeric_limits<std::size_t>::max()};
};

[[nodiscard]] RuntimeStepResult run_script_frame(
    Session* session,
    ScriptControllerId controller,
    ScriptActionId action,
    float frame) noexcept;

[[nodiscard]] std::span<const RuntimeEffectInstance> active_effect_instances(
    const Session* session) noexcept;
[[nodiscard]] std::span<const RuntimeEffectInstance>
presentation_effect_instances(const Session* session) noexcept;
[[nodiscard]] std::span<const RuntimeEffectEvent> effect_events(
    const Session* session) noexcept;

// Presentation toggle only. It never changes script events, lifetime or
// active instance state.
void set_effects_visible(Session* session, bool visible) noexcept;
[[nodiscard]] bool effects_visible(const Session* session) noexcept;

}  // namespace dmcresource::motion
