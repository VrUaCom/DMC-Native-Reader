#include "dmcresource/camera_control.h"

#include <algorithm>
#include <cmath>
#include <mutex>

#include "dmcresource/view_renderer.h"

namespace dmcresource::camera {
namespace {

std::mutex g_mutex;
State g_state;
Gesture g_owner{Gesture::None};

// Inside the ring: a 12 % dead zone and a quadratic response, fine control
// near the centre.
constexpr float kDeadZone = 0.12F;

}  // namespace

State current() noexcept {
    const std::lock_guard lock{g_mutex};
    return g_state;
}

void enter_fly(const Vec3& eye) noexcept {
    const std::lock_guard lock{g_mutex};
    g_state.fly = true;
    g_state.eye = eye;
}

void leave_fly(float yaw_radians, float pitch_radians, float framing_distance) noexcept {
    const std::lock_guard lock{g_mutex};
    if (!g_state.fly) return;
    g_state.fly = false;
    const auto basis = camera_basis(yaw_radians, pitch_radians);
    const float d = std::isfinite(framing_distance) && framing_distance > 0.0F ? framing_distance : 1.0F;
    g_state.pivot = {g_state.eye.x + basis.forward.x * d, g_state.eye.y + basis.forward.y * d,
                     g_state.eye.z + basis.forward.z * d};
    g_state.has_pivot = true;
}

void reset() noexcept {
    const std::lock_guard lock{g_mutex};
    g_state = {};
}

StickResponse stick_response(float dx_px, float dy_px, float density) noexcept {
    StickResponse out;
    const float dp = std::max(0.1F, density);
    const float radius = kStickRadiusDp * dp;
    const float distance = std::hypot(dx_px, dy_px);
    if (!std::isfinite(distance) || distance < kDeadZone * radius) return out;
    const float length = distance / radius;
    const float clamped = std::min(1.0F, length);
    const float scaled = (clamped - kDeadZone) / (1.0F - kDeadZone);
    const float k = scaled * scaled / length;
    out.x = dx_px / radius * k;
    out.y = dy_px / radius * k;

    // Past the ring: dead band, then active / rest zones in turn.
    float beyond = distance - radius - kBoostDeadBandDp * dp;
    if (beyond <= 0.0F) return out;
    const float active = kBoostActiveDp * dp;
    const float rest = kBoostRestDp * dp;
    for (int step = 0; step < kBoostSteps; ++step) {
        if (beyond < active) {
            out.boost = 1.0F + static_cast<float>(step) + beyond / active;
            out.zone = step + 1;
            return out;
        }
        beyond -= active;
        out.boost = 2.0F + static_cast<float>(step);
        out.zone = step + 1;
        if (beyond < rest) return out;
        beyond -= rest;
    }
    return out;
}

std::vector<float> stick_zone_radii(float density) {
    const float dp = std::max(0.1F, density);
    std::vector<float> radii;
    float r = kStickRadiusDp * dp;
    radii.push_back(r);
    r += kBoostDeadBandDp * dp;
    radii.push_back(r);
    for (int step = 0; step < kBoostSteps; ++step) {
        r += kBoostActiveDp * dp;
        radii.push_back(r);
        r += kBoostRestDp * dp;
        radii.push_back(r);
    }
    return radii;
}

std::pair<float, float> stick_knob(float dx_px, float dy_px, float density) noexcept {
    const float radius = kStickRadiusDp * std::max(0.1F, density);
    const float distance = std::hypot(dx_px, dy_px);
    if (!std::isfinite(distance)) return {0.0F, 0.0F};
    if (distance <= radius) return {dx_px, dy_px};
    return {dx_px / distance * radius, dy_px / distance * radius};
}

bool claim_gesture(Gesture gesture) noexcept {
    const std::lock_guard lock{g_mutex};
    if (gesture == Gesture::None) return false;
    if (g_owner == Gesture::None) g_owner = gesture;
    return g_owner == gesture;
}

bool zoom_step(float touch_zoom, float next, bool settling) noexcept {
    const std::lock_guard lock{g_mutex};
    if (g_owner == Gesture::Zoom) return true;
    if (g_owner != Gesture::None) return false;
    if (!settling && touch_zoom > 0.0F && next > 0.0F && std::isfinite(touch_zoom) && std::isfinite(next)
        && std::abs(std::log(next / touch_zoom)) >= kZoomEngage) {
        g_owner = Gesture::Zoom;
    }
    return true;
}

Gesture touch_owner() noexcept {
    const std::lock_guard lock{g_mutex};
    return g_owner;
}

void end_touch() noexcept {
    const std::lock_guard lock{g_mutex};
    g_owner = Gesture::None;
}

FlyStep fly_tick(float yaw_radians, float pitch_radians, const StickResponse& move, const StickResponse& look,
                 float dt_seconds, float speed) noexcept {
    const float dt = std::clamp(std::isfinite(dt_seconds) ? dt_seconds : 0.0F, 0.0F, 0.05F);
    FlyStep out{yaw_radians, pitch_radians, {}};
    out.yaw -= look.x * 1.8F * dt;  // push right: turn right
    out.pitch = std::clamp(out.pitch - look.y * 1.2F * dt, -1.55F, 1.55F);  // push up: look up
    const float step = std::max(0.0F, speed) * dt * std::max(1.0F, move.boost);
    const std::lock_guard lock{g_mutex};
    if (g_state.fly && (move.x != 0.0F || move.y != 0.0F)) {
        g_state.eye = fly_move(g_state.eye, out.yaw, out.pitch, -move.y * step, move.x * step, 0.0F);
    }
    out.eye = g_state.eye;
    return out;
}

}  // namespace dmcresource::camera
