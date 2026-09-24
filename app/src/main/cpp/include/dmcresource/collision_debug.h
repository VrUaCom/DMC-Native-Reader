#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "dmcresource/collision_shapes.h"
#include "dmcresource/mesh.h"

// Collision debug display. The game keeps four debug meshes in its system
// resource list (0x1405B0860): obj\debug\at000.mod .. at003.mod. They are
// NOT shipped here; the same shapes are generated in code with the measured
// dimensions:
//   at000  unit sphere (radius 1)             -> shape type 2, scaled by the radius
//   at001  cube +-1 (= corner table 0x1405CEC60) -> type 3, scaled by the half size
//   at002  capsule radius 1, segment y +-0.5  -> type 4 (rebuilt for each a/b/radius)
//   at003  octagonal prism y +-1              -> type 6 (no sample record yet)
// Evidence: dmc-rengine-cpp docs/research/dmc3-collision-tables-2026-09-24.md.
namespace dmcresource {
struct Session;
}

namespace dmcresource::collision {

// Line list: `lines` holds vertex index pairs.
struct WireMesh final {
    std::vector<Vec3> vertices;
    std::vector<std::uint32_t> lines;
};

[[nodiscard]] const WireMesh& debug_sphere();    // at000
[[nodiscard]] const WireMesh& debug_box();       // at001
[[nodiscard]] const WireMesh& debug_capsule();   // at002 (unit: radius 1, half length 0.5)
[[nodiscard]] const WireMesh& debug_cylinder();  // at003

// Capsule wire for segment a-b and radius r (same rings as at002).
[[nodiscard]] WireMesh capsule_wire(const Vec3& a, const Vec3& b, float radius);

// One shape in bone space as a line list (pairs of points).
[[nodiscard]] std::vector<Vec3> shape_lines(const Shape& shape);

// Collision handle of an assembled character: attack index + shapes, bound
// to the scene nodes of the model whose bone matrices the attacks use.
struct CollisionBinding final {
    std::string name;
    std::vector<AttackEntry> attacks;
    std::vector<Shape> shapes;
    std::size_t node_begin{};
    std::size_t node_count{};
    // -1: every used attack; >= 0: that attack id only.
    int attack{-1};
};

// World-space line pairs of the selected attack(s) on the current pose.
[[nodiscard]] std::vector<Vec3> posed_collision_lines(const Session& session);

// Used attack ids (mask != 0 and a valid bone/shape) of the session binding.
[[nodiscard]] std::vector<int> collision_attack_ids(const Session& session);

// Selects the attack to show (-1 = all). Returns false without a binding.
bool select_collision_attack(Session* session, int attack) noexcept;

// "attack 12: bone 9 sphere #12" style description of the selection.
[[nodiscard]] std::string describe_collision_selection(const Session& session);

}  // namespace dmcresource::collision
