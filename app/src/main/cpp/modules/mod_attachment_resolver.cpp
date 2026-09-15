#include "dmcresource/mod_attachment_resolver.h"

#include <cstdint>
#include <limits>
#include <vector>

#include "dmc_rengine/formats/mod/attachment.hpp"

namespace dmcresource::mod_attachment_resolver {
namespace {

namespace canonical_attachment = dmc::rengine::formats::mod::attachment;
namespace canonical_world = dmc::rengine::formats::mod::world_transform;

[[nodiscard]] canonical_world::Matrix4f to_canonical(const Matrix4& matrix) noexcept {
    canonical_world::Matrix4f out{};
    out.values = matrix.values;
    return out;
}

[[nodiscard]] Matrix4 from_canonical(const canonical_world::Matrix4f& matrix) noexcept {
    Matrix4 out{};
    out.values = matrix.values;
    return out;
}

}  // namespace

ResolveResult resolve_default_joint(
    const CompositePart& child,
    const CompositePart& host) noexcept {
    ResolveResult out;
    if (!child.scene.default_attachment_selector.has_value()) {
        out.status = ResolveStatus::MissingSelector;
        return out;
    }

    const auto selector32 = *child.scene.default_attachment_selector;
    out.selector = selector32;
    if (selector32 > static_cast<std::uint32_t>(
            std::numeric_limits<std::uint8_t>::max())) {
        out.status = ResolveStatus::SelectorNotU8;
        return out;
    }

    if (host.scene.nodes.empty()) {
        out.status = ResolveStatus::HostSpatialHierarchyUnavailable;
        return out;
    }

    try {
        std::vector<canonical_world::Matrix4f> host_world;
        host_world.reserve(host.scene.nodes.size());
        for (const auto& node : host.scene.nodes) {
            if (!node.spatial_authority) {
                out.status = ResolveStatus::HostSpatialHierarchyUnavailable;
                return out;
            }
            host_world.push_back(to_canonical(node.world));
        }

        const auto resolved = canonical_attachment::resolve_default_joint(
            static_cast<std::uint8_t>(selector32), host_world);
        if (!resolved.ok()) {
            out.status = resolved.status == canonical_attachment::ResolveStatus::selector_out_of_range
                ? ResolveStatus::SelectorOutOfRange
                : ResolveStatus::HostSpatialHierarchyUnavailable;
            return out;
        }

        out.root_matrix = from_canonical(resolved.host_joint_world);
        out.status = ResolveStatus::Resolved;
        return out;
    } catch (...) {
        out.status = ResolveStatus::HostSpatialHierarchyUnavailable;
        return out;
    }
}

}  // namespace dmcresource::mod_attachment_resolver
