#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

#include "dmcresource/render_scene.h"

namespace dmcresource {

inline constexpr std::size_t kNoCompositePart =
    std::numeric_limits<std::size_t>::max();
inline constexpr std::uint32_t kNoAttachmentSelector =
    std::numeric_limits<std::uint32_t>::max();

enum class CompositePlacementMode : std::uint8_t {
    SourceCoordinates,
    HostJoint,
};

// Derived placement state for one source MOD inside a composite session.
// The source-local RenderScene remains authoritative and is never rewritten.
// Placement only changes the top-level render/hierarchy projection.
struct CompositePlacement final {
    CompositePlacementMode mode{CompositePlacementMode::SourceCoordinates};
    std::size_t host_part_index{kNoCompositePart};
    std::uint32_t attachment_selector{kNoAttachmentSelector};
    Matrix4 root_matrix{};
    bool resolved{false};
};

// One canonical source MOD inside a composite scene. The source RenderScene and
// local texture-slot projection are retained intact so animation, attachment,
// texture and future physics modules can address parts without reconstructing
// ownership from the flattened renderer cache.
struct CompositePart final {
    std::string name;
    RenderScene scene;
    std::vector<std::uint32_t> render_triangle_texture_slots;
    std::uint32_t texture_slot_base{};
    std::uint32_t texture_slot_span{};
    bool texture_companion_attached{};
    std::string texture_attachment_detail;
    CompositePlacement placement;
};

}  // namespace dmcresource
