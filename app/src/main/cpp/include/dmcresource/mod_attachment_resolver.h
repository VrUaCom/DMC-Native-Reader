#pragma once

#include <cstddef>
#include <cstdint>

#include "dmcresource/composite_model.h"

namespace dmcresource::mod_attachment_resolver {

enum class ResolveStatus : std::uint8_t {
    Resolved,
    MissingSelector,
    SelectorNotU8,
    HostSpatialHierarchyUnavailable,
    SelectorOutOfRange,
};

struct ResolveResult final {
    ResolveStatus status{ResolveStatus::MissingSelector};
    std::uint32_t selector{kNoAttachmentSelector};
    Matrix4 root_matrix{};

    [[nodiscard]] constexpr bool ok() const noexcept {
        return status == ResolveStatus::Resolved;
    }
};

// Resolve one source-local child MOD against one explicit host MOD. Selector
// semantics and indexing are delegated to the pinned DMC Rengine ReaderCore.
// This function does not infer host ownership from file names or arbitrary
// resource order.
[[nodiscard]] ResolveResult resolve_default_joint(
    const CompositePart& child,
    const CompositePart& host) noexcept;

[[nodiscard]] constexpr const char* to_string(ResolveStatus status) noexcept {
    switch (status) {
        case ResolveStatus::Resolved: return "resolved";
        case ResolveStatus::MissingSelector: return "missing-selector";
        case ResolveStatus::SelectorNotU8: return "selector-not-u8";
        case ResolveStatus::HostSpatialHierarchyUnavailable:
            return "host-spatial-hierarchy-unavailable";
        case ResolveStatus::SelectorOutOfRange: return "selector-out-of-range";
    }
    return "unknown";
}

}  // namespace dmcresource::mod_attachment_resolver
