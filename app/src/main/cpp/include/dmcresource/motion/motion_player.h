#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

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

}  // namespace dmcresource::motion
