#pragma once

#include <vector>

#include "dmcresource/mesh.h"

// The viewer's camera decisions: the fly camera (its eye), the orbit pivot it
// leaves behind, and how a joystick under a finger turns into motion. A
// platform shell forwards finger positions and the screen density and draws
// what it is told; it does not shape the response.
namespace dmcresource::camera {

// ---- state shared by every render, pick and benchmark (thread-safe) --------

struct State final {
    bool fly{false};
    Vec3 eye{};
    // After a flight the orbit turns round the point the fly camera looked
    // at instead of the framed model, so leaving fly keeps the picture.
    bool has_pivot{false};
    Vec3 pivot{};
};

[[nodiscard]] State current() noexcept;
void enter_fly(const Vec3& eye) noexcept;
// Leaves fly: the orbit pivot is set `framing_distance` ahead of the eye
// along the view; with pan and dolly at zero the orbit camera stands where
// the fly camera stood, looking the same way.
void leave_fly(float yaw_radians, float pitch_radians, float framing_distance) noexcept;
// Back to framing the model (Reset view, another file).
void reset() noexcept;

// ---- joysticks -------------------------------------------------------------

// A stick knob's travel to full deflection, and past it the boost zones: a
// dead band, then active zones (the multiplier climbs across each) and rest
// zones (it holds) in turn, so pulling the finger further out speeds up in
// steps the hand can feel. Sizes are in dp, scaled by the screen density.
inline constexpr float kStickRadiusDp = 49.0F;
inline constexpr float kBoostDeadBandDp = 18.0F;
inline constexpr float kBoostActiveDp = 28.0F;
inline constexpr float kBoostRestDp = 14.0F;
inline constexpr int kBoostSteps = 5;  // boost reaches 1 + kBoostSteps

struct StickResponse final {
    float x{};       // -1..1, right
    float y{};       // -1..1, down
    float boost{1.0F};
    int zone{0};     // 0 inside the ring / dead band, then 1.. per active zone reached
};
// The finger `dx`, `dy` pixels from the stick's base on a screen of `density`.
[[nodiscard]] StickResponse stick_response(float dx_px, float dy_px, float density) noexcept;
// Ring radii in pixels for the HUD: the knob's travel, then the end of the
// dead band, then the end of every active and rest zone in turn.
[[nodiscard]] std::vector<float> stick_zone_radii(float density);

// One frame of flying: the look stick turns (1.8 rad/s) and tilts
// (1.2 rad/s); the move stick moves the eye `speed` model units per second
// at full deflection, times its boost. The eye lives in State.
struct FlyStep final {
    float yaw{};
    float pitch{};
    Vec3 eye{};
};
[[nodiscard]] FlyStep fly_tick(float yaw_radians, float pitch_radians, const StickResponse& move,
                               const StickResponse& look, float dt_seconds, float speed) noexcept;

}  // namespace dmcresource::camera
