#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "dmcresource/mesh.h"

namespace dmcresource::environment_collision {

// HITS is the stage/environment collision family.  It is deliberately kept
// separate from collision_shapes.h: the latter is the character attack
// collision table bound to body bones, while HITS stores world-space
// triangle-plane records and a spatial cell index.
struct Triangle final {
    std::uint32_t flags{};
    Vec3 point_a{};
    Vec3 point_b{};
    Vec3 point_c{};
    Vec3 normal{};
    float plane_d{};
};

struct Source final {
    std::string resource_name;
    std::uint32_t resource_slot{};

    Vec3 bounds_min{};
    Vec3 bounds_max{};
    // The current vendored parser exposes +0x20 as a Vec3f, but the supplied
    // retail surfaces carry exact integer-looking little-endian words there
    // (500/500/500 and 300/300/300). Preserve the raw lane until its EXE
    // scalar encoding is independently closed; do not use it for transforms
    // or gameplay queries.
    std::array<std::uint32_t, 3> cell_size_raw{};
    std::uint32_t grid_count_x{};
    std::uint32_t grid_count_y{};
    std::uint32_t grid_count_z{};
    std::size_t cell_reference_count{};

    std::vector<Triangle> triangles;

    // The source ordering and physical slot are preserved.  For DMC3 stage
    // PACs the first HITS source is the detailed source-0 path and a later
    // source may be the optional coarse source-1 path; this is provenance,
    // not a universal semantic guess for other game profiles.
    const char* evidence{"EXE_AND_CORPUS_CONFIRMED"};
};

[[nodiscard]] bool looks_like(std::span<const std::uint8_t> bytes) noexcept;

// Read-only, fail-closed projection of the canonical Rengine HITS parser.
// Unknown header/flag semantics remain outside this product projection.
[[nodiscard]] std::optional<Source> parse(std::string_view resource_name,
                                           std::uint32_t resource_slot,
                                           std::span<const std::uint8_t> bytes) noexcept;

// Room-local line pairs for the optional collision inspection overlay.  The
// renderer applies the same room pivot/yaw/offset as the visible stage mesh.
[[nodiscard]] std::vector<Vec3> debug_lines(const Source& source);

}  // namespace dmcresource::environment_collision
