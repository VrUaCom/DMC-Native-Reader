// The view's touch math (#45): the numbers DmcRenderView used on Android,
// now the core's for every shell.
#include "dmcresource/view_gesture.h"

#include <cassert>
#include <cmath>
#include <numbers>

namespace g = dmcresource::view_gesture;

namespace {
bool near(float a, float b, float eps = 1.0e-5F) { return std::fabs(a - b) <= eps; }
}  // namespace

int main() {
    // Start: a front three-quarter view.
    const auto start = g::default_orbit();
    assert(near(start.yaw, std::numbers::pi_v<float> - 0.65F) && near(start.pitch, -0.45F) && start.zoom == 1.0F);
    assert(start.pan_x == 0.0F && start.pan_y == 0.0F && start.room_yaw == 0.0F);

    // A drag of 100 px right, 50 px down: grab-and-turn at 0.008 rad/px.
    auto o = start;
    assert(g::drag(o, 100.0F, 50.0F));
    assert(near(o.yaw, start.yaw - 0.8F) && near(o.pitch, start.pitch - 0.4F));
    assert(!g::drag(o, 0.0F, 0.0F) && !g::drag(o, NAN, 1.0F));
    // Pitch stops short of straight up and down.
    assert(g::drag(o, 0.0F, -10000.0F) && near(o.pitch, g::kPitchLimit));
    assert(g::drag(o, 0.0F, 10000.0F) && near(o.pitch, -g::kPitchLimit));

    // Two fingers 74 px right on a 740 px short side at zoom 1: 0.296 framing radii, content follows.
    o = start;
    assert(g::pan(o, 74.0F, -74.0F, 1000, 740));
    assert(near(o.pan_x, -0.296F) && near(o.pan_y, -0.296F));
    // Zoomed in twice, the same finger travel moves half as far; a zero-size view is treated as 1 px.
    auto z = start;
    z.zoom = 2.0F;
    assert(g::pan(z, 74.0F, 0.0F, 740, 2000) && near(z.pan_x, -0.148F));
    auto tiny = start;
    assert(g::pan(tiny, 1.0F, 0.0F, 0, 0) && std::isfinite(tiny.pan_x));

    // Twist: wrapping across +-pi takes the short way round.
    assert(near(g::angle_delta(3.0F, -3.0F), 2.0F * std::numbers::pi_v<float> - 6.0F, 1.0e-4F));
    assert(near(g::angle_delta(0.1F, 0.3F), 0.2F) && g::angle_delta(NAN, 1.0F) == 0.0F);
    assert(near(g::angle_delta(0.0F, std::numbers::pi_v<float>), std::numbers::pi_v<float>, 1.0e-5F));
    o = start;
    assert(g::twist(o, 0.25F, false) && near(o.yaw, start.yaw - 0.25F) && o.room_yaw == 0.0F);
    assert(g::twist(o, 0.25F, true) && near(o.room_yaw, -0.25F));
    assert(!g::twist(o, 0.0001F, true));  // finger jitter

    // Pinch: proportional to the scale since the touch began, within 0.15..8.
    assert(near(g::pinch_zoom(1.0F, 2.0F), 2.0F) && near(g::pinch_zoom(2.0F, 0.5F), 1.0F));
    assert(g::pinch_zoom(1.0F, 100.0F) == g::kZoomMax && g::pinch_zoom(1.0F, 0.001F) == g::kZoomMin);
    assert(g::pinch_zoom(0.0F, NAN) == 1.0F);

    // Fling: below 900 px/s on a 2.5x screen nothing spins; above, it spins and slows to a stop.
    assert(!g::fling(300.0F, 0.0F, 2.5F).active());
    auto spin = g::fling(2000.0F, 1000.0F, 2.5F);
    assert(spin.active() && near(spin.yaw_rate, 16.0F) && near(spin.pitch_rate, 4.0F));
    // On a desktop pointer (density 1) the threshold is 360 px/s.
    assert(g::fling(400.0F, 0.0F, 1.0F).active() && !g::fling(300.0F, 0.0F, 1.0F).active());
    o = start;
    const float yaw0 = o.yaw;
    assert(g::spin_step(o, spin, 0.016F));
    assert(near(o.yaw, yaw0 - 16.0F * 0.016F) && near(spin.yaw_rate, 16.0F * std::pow(0.08F, 0.016F), 1.0e-4F));
    // A long gap counts as 0.05 s at most.
    auto big = g::fling(2000.0F, 0.0F, 2.5F);
    auto o2 = start;
    assert(g::spin_step(o2, big, 5.0F) && near(o2.yaw, start.yaw - 16.0F * 0.05F));
    int steps = 0;
    while (g::spin_step(o, spin, 0.016F)) ++steps;
    assert(steps > 10 && steps < 400 && !spin.active() && spin.yaw_rate == 0.0F && spin.pitch_rate == 0.0F);
    assert(o.pitch >= -g::kPitchLimit && o.pitch <= g::kPitchLimit);
    return 0;
}
