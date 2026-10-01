// Fly camera (view_renderer.h): the eye of an orbit view, switching to fly
// with that eye keeps the picture, and the basis matches the screen.
#include <cassert>
#include <cmath>
#include <vector>

#include "dmcresource/scene_projection.h"
#include "dmcresource/view_renderer.h"

using namespace dmcresource;

namespace {

constexpr int W = 640, H = 480;

std::vector<HierarchyScreenPoint> project(const Mesh& mesh, const ViewState& view, const std::vector<Vec3>& points) {
    HierarchyOverlay overlay;
    overlay.spatial = true;
    overlay.points = points;
    overlay.kinds.assign(points.size(), RenderNodeKind{});
    return project_hierarchy_points(mesh, overlay, W, H, view);
}

Vec3 add(const Vec3& a, const Vec3& b, float k) { return {a.x + b.x * k, a.y + b.y * k, a.z + b.z * k}; }

}  // namespace

int main() {
    Mesh mesh;
    for (int i = 0; i < 8; ++i) {
        mesh.vertices.push_back({(i & 1) ? 40.0F : -40.0F, (i & 2) ? 180.0F : 0.0F, (i & 4) ? 30.0F : -30.0F});
    }
    mesh.indices = {0U, 1U, 2U, 1U, 3U, 2U};
    const std::vector<Vec3> probes{{0.0F, 90.0F, 0.0F}, {35.0F, 10.0F, -20.0F}, {-30.0F, 170.0F, 25.0F}};

    for (const float yaw : {0.0F, 0.65F, 2.4F, -1.3F}) {
        for (const float pitch : {-0.45F, 0.0F, 0.7F}) {
            ViewState orbit;
            orbit.yaw_radians = yaw;
            orbit.pitch_radians = pitch;
            orbit.zoom = 1.4F;
            orbit.pan_x = 0.15F;
            orbit.pan_y = -0.2F;
            orbit.dolly = 0.3F;
            const Vec3 eye = view_camera_eye(mesh, W, H, orbit);

            // Same picture after the switch.
            ViewState fly = orbit;
            fly.fly = true;
            fly.fly_eye = eye;
            const auto a = project(mesh, orbit, probes);
            const auto b = project(mesh, fly, probes);
            for (std::size_t i = 0U; i < probes.size(); ++i) {
                assert(std::fabs(a[i].x - b[i].x) < 0.02F && std::fabs(a[i].y - b[i].y) < 0.02F);
            }
            assert(view_camera_eye(mesh, W, H, fly).x == eye.x);

            // Forward is the screen centre; right and up are screen right and up.
            const auto basis = camera_basis(yaw, pitch);
            const Vec3 ahead = add(eye, basis.forward, 300.0F);
            const auto c = project(mesh, fly, {ahead, add(ahead, basis.right, 20.0F), add(ahead, basis.up, 20.0F)});
            assert(std::fabs(c[0].x - W * 0.5F) < 0.05F && std::fabs(c[0].y - H * 0.5F) < 0.05F);
            assert(c[1].x > W * 0.5F + 1.0F && std::fabs(c[1].y - H * 0.5F) < 0.05F);
            assert(c[2].y < H * 0.5F - 1.0F && std::fabs(c[2].x - W * 0.5F) < 0.05F);

            // Steps: forward along the view, strafe along right, rise straight up.
            const Vec3 moved = fly_move(eye, yaw, pitch, 10.0F, -4.0F, 3.0F);
            const Vec3 expect = add(add(add(eye, basis.forward, 10.0F), basis.right, -4.0F), {0.0F, 1.0F, 0.0F}, 3.0F);
            assert(std::fabs(moved.x - expect.x) < 1.0e-4F && std::fabs(moved.y - expect.y) < 1.0e-4F &&
                   std::fabs(moved.z - expect.z) < 1.0e-4F);

            // Turning in place keeps the eye: a point at the eye's side stays
            // on that side, the centre follows the new direction.
            ViewState turned = fly;
            turned.yaw_radians = yaw + 0.5F;
            const auto t = project(mesh, turned, {add(eye, camera_basis(yaw + 0.5F, pitch).forward, 300.0F)});
            assert(std::fabs(t[0].x - W * 0.5F) < 0.05F && std::fabs(t[0].y - H * 0.5F) < 0.05F);
        }
    }

    // A fly view renders (software path) and its picture is not empty.
    {
        ViewState view;
        view.fly = true;
        view.fly_eye = view_camera_eye(mesh, W, H, view);
        view.fly_eye = fly_move(view.fly_eye, view.yaw_radians, view.pitch_radians, 50.0F, 0.0F, 0.0F);
        const auto image = render_view(mesh, W, H, view);
        assert(image.width == W && image.height == H);
        std::size_t lit = 0U;
        for (std::size_t i = 0U; i < image.pixels.size(); i += 4U) lit += image.pixels[i] > 60U;
        assert(lit > 100U);
    }
    return 0;
}
