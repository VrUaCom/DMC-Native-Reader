#pragma once

// The view's touch and pointer math (#45), shared by every shell. A shell
// reports what the fingers or the pointer did; the core turns that into the
// orbit camera's yaw, pitch, zoom, pan and the room's yaw, so a drag, a
// pinch, a twist or a flick moves a model the same way on Android, macOS,
// iOS and Windows. Pure functions on a value: no session, no lock.

#include <cstdint>

namespace dmcresource::view_gesture {

struct Orbit final {
    float yaw{};
    float pitch{};
    float zoom{1.0F};
    float pan_x{};
    float pan_y{};
    float room_yaw{};
};

// DMC3 models face +Z while the camera looks along +Z, so yaw 0 shows the
// back: a model starts from a front three-quarter view.
[[nodiscard]] Orbit default_orbit() noexcept;

inline constexpr float kPitchLimit = 1.55F;       // radians, just short of straight up/down
inline constexpr float kRadiansPerPixel = 0.008F; // one finger: grab-and-turn
inline constexpr float kZoomMin = 0.15F;
inline constexpr float kZoomMax = 8.0F;

// One finger (or the pointer) moved by dx, dy pixels: the surface under it
// follows it. Returns false when nothing changed.
bool drag(Orbit& orbit, float dx, float dy) noexcept;

// Two fingers moved together by dx, dy pixels over a width x height view:
// the content under them follows them in the camera plane.
bool pan(Orbit& orbit, float dx, float dy, int view_width, int view_height) noexcept;

// The signed turn from angle `from` to angle `to`, both radians, in (-pi, pi].
[[nodiscard]] float angle_delta(float from, float to) noexcept;

// Two fingers turned by `radians`: with a room drawn the model turns in it,
// without one the view turns.
bool twist(Orbit& orbit, float radians, bool room_shown) noexcept;

// A pinch by `scale` since the touch began at `touch_zoom`: the zoom then,
// within kZoomMin..kZoomMax.
[[nodiscard]] float pinch_zoom(float touch_zoom, float scale) noexcept;

// A flick at release: its speed in pixels per second on a screen of
// `density` (pixels per dp; 1 on a desktop pointer). Starts the inertia
// turn when fast enough.
struct Spin final {
    float yaw_rate{};    // radians per second
    float pitch_rate{};
    [[nodiscard]] bool active() const noexcept;
};
[[nodiscard]] Spin fling(float velocity_x, float velocity_y, float density) noexcept;

// Advances the inertia by `dt_seconds` (at most 0.05 per step): turns the
// orbit and slows the spin (about -92 % per second). Returns false once the
// spin has stopped (it is then zero).
bool spin_step(Orbit& orbit, Spin& spin, float dt_seconds) noexcept;

}  // namespace dmcresource::view_gesture
