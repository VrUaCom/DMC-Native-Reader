// Camera decisions of the core (camera_control.h): leaving the fly camera
// keeps the picture (the orbit turns round where the flight looked), and the
// joystick speeds up in steps past its ring.
#include <cassert>
#include <cmath>

#include "dmcresource/camera_control.h"
#include "dmcresource/resource_session.h"
#include "dmcresource/view_renderer.h"

using namespace dmcresource;

namespace {

ViewControls controls_now() {
    ViewControls controls;
    const auto state = camera::current();
    controls.fly = state.fly;
    controls.eye = state.eye;
    controls.has_pivot = state.has_pivot;
    controls.pivot = state.pivot;
    return controls;
}

bool near(const Vec3& a, const Vec3& b, float tolerance) {
    return std::fabs(a.x - b.x) < tolerance && std::fabs(a.y - b.y) < tolerance && std::fabs(a.z - b.z) < tolerance;
}

}  // namespace

int main() {
    Session model;
    model.renderable = true;
    model.capabilities = capability(ResourceCapability::Geometry);
    for (int i = 0; i < 8; ++i) {
        model.render_mesh.vertices.push_back(
            {(i & 1) ? 40.0F : -40.0F, (i & 2) ? 180.0F : 0.0F, (i & 4) ? 30.0F : -30.0F});
    }
    model.render_mesh.indices = {0U, 1U, 2U, 1U, 3U, 2U};

    camera::reset();
    const float yaw = 0.7F, pitch = -0.3F;
    // Orbit -> fly at the orbit eye.
    const auto orbit_eye = session_camera_eye(&model, 640, 480, yaw, pitch, 1.0F, 0U, controls_now());
    assert(orbit_eye);
    camera::enter_fly(*orbit_eye);
    // Fly away: forward and sideways for a second, turning a little.
    const auto move = camera::stick_response(30.0F, -60.0F, 2.0F);
    const auto look = camera::stick_response(20.0F, 0.0F, 2.0F);
    float y = yaw, p = pitch;
    for (int frame = 0; frame < 20; ++frame) {
        const auto step = camera::fly_tick(y, p, move, look, 0.05F, 100.0F);
        y = step.yaw;
        p = step.pitch;
    }
    const auto flown = camera::current().eye;
    assert(!near(flown, *orbit_eye, 1.0F));
    const auto fly_eye = session_camera_eye(&model, 640, 480, y, p, 1.0F, 0U, controls_now());
    assert(fly_eye && near(*fly_eye, flown, 1.0e-3F));
    // Fly -> orbit: the orbit camera stands where the flight ended.
    camera::leave_fly(y, p, session_camera_distance(&model));
    const auto state = camera::current();
    assert(!state.fly && state.has_pivot);
    const auto back = session_camera_eye(&model, 640, 480, y, p, 1.0F, 0U, controls_now());
    assert(back && near(*back, flown, 0.05F));
    // Orbiting from there turns round the pivot, not the model.
    const auto turned = session_camera_eye(&model, 640, 480, y + 0.5F, p, 1.0F, 0U, controls_now());
    assert(turned);
    const float d0 = std::hypot(back->x - state.pivot.x, back->z - state.pivot.z);
    const float d1 = std::hypot(turned->x - state.pivot.x, turned->z - state.pivot.z);
    assert(std::fabs(d0 - d1) < 0.05F);
    // Reset view: the model is framed again.
    camera::reset();
    const auto framed = session_camera_eye(&model, 640, 480, y, p, 1.0F, 0U, controls_now());
    assert(framed && !near(*framed, flown, 1.0F));

    // ---- joystick ----
    const float density = 2.0F;
    const float ring = camera::kStickRadiusDp * density;
    // Inside the dead zone: nothing; at the ring: full deflection, no boost.
    assert(camera::stick_response(3.0F, 0.0F, density).x == 0.0F);
    const auto full = camera::stick_response(ring, 0.0F, density);
    assert(std::fabs(full.x - 1.0F) < 1.0e-4F && full.boost == 1.0F && full.zone == 0);
    // Past the ring but inside the dead band: still no boost.
    assert(camera::stick_response(ring + 10.0F * density, 0.0F, density).boost == 1.0F);
    // Half through the first active zone: x1.5; across a rest zone it holds x2.
    const float active0 = ring + camera::kBoostDeadBandDp * density;
    const auto half = camera::stick_response(0.0F, active0 + camera::kBoostActiveDp * density * 0.5F, density);
    assert(std::fabs(half.boost - 1.5F) < 1.0e-3F && half.zone == 1 && std::fabs(half.y - 1.0F) < 1.0e-4F);
    const float rest0 = active0 + camera::kBoostActiveDp * density;
    assert(std::fabs(camera::stick_response(rest0 + 2.0F, 0.0F, density).boost - 2.0F) < 1.0e-4F);
    assert(std::fabs(camera::stick_response(rest0 + camera::kBoostRestDp * density - 1.0F, 0.0F, density).boost -
                     2.0F) < 1.0e-4F);
    // Far out: capped at 1 + steps.
    assert(camera::stick_response(5000.0F, 0.0F, density).boost == 1.0F + camera::kBoostSteps);
    // Zone rings: travel, dead band, then 2 per step, growing.
    const auto rings = camera::stick_zone_radii(density);
    assert(rings.size() == 2U + 2U * camera::kBoostSteps && rings[0] == ring);
    for (std::size_t i = 1; i < rings.size(); ++i) assert(rings[i] > rings[i - 1]);
    // A boosted move covers more ground in the same time.
    camera::enter_fly({0.0F, 0.0F, 0.0F});
    const auto slow = camera::fly_tick(0.0F, 0.0F, camera::stick_response(0.0F, -ring, density), {}, 0.05F, 100.0F);
    camera::enter_fly({0.0F, 0.0F, 0.0F});
    const auto fast = camera::fly_tick(0.0F, 0.0F, camera::stick_response(0.0F, -5000.0F, density), {}, 0.05F, 100.0F);
    const auto length = [](const Vec3& v) { return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z); };
    assert(std::fabs(length(fast.eye) / length(slow.eye) - (1.0F + camera::kBoostSteps)) < 1.0e-3F);
    camera::reset();
    return 0;
}
