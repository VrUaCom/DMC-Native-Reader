#pragma once

#include <cstddef>
#include <cstdint>

#include "dmcresource/composite_placement.h"

namespace dmcresource {
struct Session;
}

namespace dmcresource::spider::actions {

// Explicit placement action. Callers choose host/child identity; the native core
// resolves only evidence-backed spatial data and never guesses relationships
// from filenames or selection order.
[[nodiscard]] composite_placement::PlacementResult attach_mod_part_to_host_joint(
    Session* session,
    std::size_t host_part_index,
    std::size_t child_part_index,
    std::uint32_t host_joint_index) noexcept;

[[nodiscard]] composite_placement::PlacementResult reset_mod_part_placement(
    Session* session,
    std::size_t child_part_index) noexcept;

}  // namespace dmcresource::spider::actions
