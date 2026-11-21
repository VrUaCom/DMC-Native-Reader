#include "dmcresource/view_gesture.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace dmcresource::view_gesture {

namespace {

float clamp_pitch(float pitch) noexcept { return std::clamp(pitch, -kPitchLimit, kPitchLimit); }

bool finite(float value) noexcept { return std::isfinite(value); }

}  // namespace

Orbit default_orbit() noexcept {
    Orbit orbit;
    orbit.yaw = std::numbers::pi_v<float> - 0.65F;
    orbit.pitch = -0.45F;
    orbit.zoom = 1.0F;
    return orbit;
}

bool drag(Orbit& orbit, float dx, float dy) noexcept {
    if (!finite(dx) || !finite(dy) || (dx == 0.0F && dy == 0.0F)) return false;
    // Grab-and-turn: the surface under the finger follows it.
    orbit.yaw -= dx * kRadiansPerPixel;
    orbit.pitch = clamp_pitch(orbit.pitch - dy * kRadiansPerPixel);
    return true;
}

bool pan(Orbit& orbit, float dx, float dy, int view_width, int view_height) noexcept {
    if (!finite(dx) || !finite(dy) || (dx == 0.0F && dy == 0.0F)) return false;
    // Camera-plane pan in framing radii: 2.96 is the camera distance over
    // the focal ratio, so the content under the fingers stays under them.
    const int side = std::max(1, std::min(view_width, view_height));
    const float zoom = orbit.zoom > 0.0F ? orbit.zoom : 1.0F;
    const float unit = 2.96F / (zoom * static_cast<float>(side));
    orbit.pan_x -= dx * unit;
    orbit.pan_y += dy * unit;
    return true;
}

float angle_delta(float from, float to) noexcept {
    if (!finite(from) || !finite(to)) return 0.0F;
    constexpr float pi = std::numbers::pi_v<float>;
    float delta = std::remainder(to - from, 2.0F * pi);
    if (delta <= -pi) delta += 2.0F * pi;
    return delta;
}

bool twist(Orbit& orbit, float radians, bool room_shown) noexcept {
    if (!finite(radians) || std::fabs(radians) <= 0.0005F) return false;
    if (room_shown) {
        orbit.room_yaw -= radians;
    } else {
        orbit.yaw -= radians;
    }
    return true;
}

float pinch_zoom(float touch_zoom, float scale) noexcept {
    if (!finite(touch_zoom) || touch_zoom <= 0.0F) touch_zoom = 1.0F;
    if (!finite(scale) || scale <= 0.0F) scale = 1.0F;
    return std::clamp(touch_zoom * scale, kZoomMin, kZoomMax);
}

bool Spin::active() const noexcept { return std::fabs(yaw_rate) + std::fabs(pitch_rate) > 0.05F; }

Spin fling(float velocity_x, float velocity_y, float density) noexcept {
    if (!finite(velocity_x) || !finite(velocity_y)) return {};
    if (!finite(density) || density <= 0.0F) density = 1.0F;
    // A flick: faster than 900 px/s on a 2.5x screen, scaled by density.
    if (std::hypot(velocity_x, velocity_y) <= 900.0F * density / 2.5F) return {};
    return {velocity_x * kRadiansPerPixel, velocity_y * kRadiansPerPixel * 0.5F};
}

bool spin_step(Orbit& orbit, Spin& spin, float dt_seconds) noexcept {
    if (!spin.active()) {
        spin = {};
        return false;
    }
    const float dt = finite(dt_seconds) ? std::clamp(dt_seconds, 0.0F, 0.05F) : 0.0F;
    orbit.yaw -= spin.yaw_rate * dt;
    orbit.pitch = clamp_pitch(orbit.pitch - spin.pitch_rate * dt);
    const float decay = std::pow(0.08F, dt);  // about -92 % per second
    spin.yaw_rate *= decay;
    spin.pitch_rate *= decay;
    if (!spin.active()) {
        spin = {};
        return false;
    }
    return true;
}

}  // namespace dmcresource::view_gesture
