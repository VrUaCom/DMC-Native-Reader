#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

#include "dmcresource/render_scene.h"
#include "dmcresource/workspace_graph.h"

namespace dmcresource {

inline constexpr std::size_t kNoCompositePart =
    std::numeric_limits<std::size_t>::max();
inline constexpr std::uint32_t kNoAttachmentSelector =
    std::numeric_limits<std::uint32_t>::max();

enum class CompositePlacementMode : std::uint8_t {
    SourceCoordinates,
    HostJoint,
    // The part's own skeleton hangs from a host joint: its root node's world
    // is the host joint's current world (root local forced to identity when
    // requested), children compose normally and the mesh is re-skinned. This
    // is how IPlayer classes drive the coat model (see motion/part_attachment.h).
    HostJointSkeleton,
};

// Derived placement state for one source MOD inside a composite session.
// The source-local RenderScene remains authoritative and is never rewritten.
// Stable host_instance_id is semantic workspace identity; host_part_index is a
// derived cache used only to address the current flattened presentation order.
struct CompositePlacement final {
    CompositePlacementMode mode{CompositePlacementMode::SourceCoordinates};
    InstanceId host_instance_id{kInvalidInstanceId};
    std::size_t host_part_index{kNoCompositePart};
    std::uint32_t attachment_selector{kNoAttachmentSelector};
    Matrix4 root_matrix{};
    bool resolved{false};
    bool root_local_identity{false};
    // HostJointSkeleton: root base = attachment_offset x host joint world
    // (identity for the coat; the weapon record local for weapons).
    Matrix4 attachment_offset{};
};

// One canonical source MOD inside a composite scene. asset_id / instance_id are
// stable workspace identities and move with the part if presentation order is
// changed. The source RenderScene and local texture-slot projection remain the
// format authority; flattened renderer caches are derived only.
struct CompositePart final {
    AssetId asset_id{kInvalidAssetId};
    InstanceId instance_id{kInvalidInstanceId};
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
