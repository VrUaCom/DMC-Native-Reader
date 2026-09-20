#pragma once

#include <cstddef>
#include <cstdint>

#include "dmcresource/composite_model.h"

namespace dmcresource {

struct Session;

namespace composite_placement {

enum class PlacementStatus : std::uint8_t {
    Applied,
    Reset,
    InvalidSession,
    InvalidPartIndex,
    SamePart,
    HostJointUnavailable,
    HostJointWithoutSpatialAuthority,
    SourceProjectionFailed,
    CompositeProjectionMismatch,
    MatrixRejected,
    AllocationFailed,
};

struct PlacementResult final {
    PlacementStatus status{PlacementStatus::InvalidSession};
    Matrix4 root_matrix{};

    [[nodiscard]] constexpr bool ok() const noexcept {
        return status == PlacementStatus::Applied || status == PlacementStatus::Reset;
    }
};

// Resolve one child MOD against one explicit host joint. This is a derived
// preview operation: source-local CompositePart::scene is never mutated.
[[nodiscard]] PlacementResult attach_to_host_joint(
    Session* session,
    std::size_t host_part_index,
    std::size_t child_part_index,
    std::uint32_t host_joint_index) noexcept;

// Restore one child to its source-local coordinates without reparsing bytes.
[[nodiscard]] PlacementResult reset_to_source_coordinates(
    Session* session,
    std::size_t child_part_index) noexcept;

[[nodiscard]] constexpr const char* to_string(PlacementStatus status) noexcept {
    switch (status) {
        case PlacementStatus::Applied: return "applied";
        case PlacementStatus::Reset: return "reset";
        case PlacementStatus::InvalidSession: return "invalid-session";
        case PlacementStatus::InvalidPartIndex: return "invalid-part-index";
        case PlacementStatus::SamePart: return "same-part";
        case PlacementStatus::HostJointUnavailable: return "host-joint-unavailable";
        case PlacementStatus::HostJointWithoutSpatialAuthority:
            return "host-joint-without-spatial-authority";
        case PlacementStatus::SourceProjectionFailed: return "source-projection-failed";
        case PlacementStatus::CompositeProjectionMismatch:
            return "composite-projection-mismatch";
        case PlacementStatus::MatrixRejected: return "matrix-rejected";
        case PlacementStatus::AllocationFailed: return "allocation-failed";
    }
    return "unknown";
}

}  // namespace composite_placement
}  // namespace dmcresource
