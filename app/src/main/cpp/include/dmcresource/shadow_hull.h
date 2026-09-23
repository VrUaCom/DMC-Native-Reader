#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "dmcresource/mesh.h"

namespace dmcresource {
struct Session;
namespace motion {
struct SkeletonRig;
}
}  // namespace dmcresource

// Read-only SHW shadow hulls. Reverse authority: dmc-rengine-cpp
// docs/research/dmc3-real-mod-shw-payload-binding-2026-09-01.md and
// docs/research/dmc3-shw-shadow-projection-2026-09-23.md.
//
// A hull is a closed triangle mesh with exact edge adjacency. Every vertex
// names one joint of its model (selector); the game copies that model's
// matrix palette (0x1403200D0) and transforms each vertex by the selected
// matrix (0x1403202F0 -> 0x140030A70). Vertices are in the model's rest
// space, so the palette is inverseRest x world, as for skinning. The game
// then marks light-facing triangles (0x1403204F0) and draws the shadow
// volume with DMC3_SHW.hlsl; on a flat floor its footprint is the hull's
// projection along the light direction.
namespace dmcresource::shadow {

struct Hull final {
    std::vector<Vec3> vertices;
    std::vector<std::uint8_t> selectors;
    std::vector<std::uint32_t> indices;  // 3 per triangle
    std::vector<std::array<std::uint16_t, 3>> adjacency;
};

struct HullSet final {
    float version{};
    std::uint8_t node_count{};  // header +0x11: node count of the paired MOD
    std::vector<Hull> hulls;

    [[nodiscard]] std::size_t vertex_count() const noexcept;
    [[nodiscard]] std::size_t triangle_count() const noexcept;
    [[nodiscard]] std::size_t closed_hulls() const noexcept;  // T == 2V - 4
    [[nodiscard]] std::uint32_t max_selector() const noexcept;
};

[[nodiscard]] std::optional<HullSet> parse_hulls(const std::uint8_t* bytes,
                                                 std::size_t size) noexcept;

// One SHW placed on a model inside a session: node_begin/node_count locate
// the model's nodes in Session::scene.nodes.
struct ShadowBinding final {
    std::string name;
    std::size_t node_begin{};
    std::size_t node_count{};
    std::shared_ptr<const motion::SkeletonRig> rig;
    HullSet hulls;
};

// Viewer light: the game takes the light position from the stage
// (shw +0x60); without a stage the viewer uses this fixed direction.
inline constexpr Vec3 kViewerLightDirection{0.35F, -1.0F, 0.25F};

// Current hull vertices of every binding (selector joint skin matrices).
[[nodiscard]] std::vector<Vec3> posed_hull_triangles(const Session& session);

// Floor footprint: posed hull triangles projected along `light` onto y = floor_y.
[[nodiscard]] std::vector<Vec3> floor_shadow_triangles(const Session& session,
                                                       Vec3 light,
                                                       float floor_y);

}  // namespace dmcresource::shadow
