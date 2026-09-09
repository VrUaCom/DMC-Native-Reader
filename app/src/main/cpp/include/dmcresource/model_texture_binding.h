#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "dmcresource/mesh.h"

namespace dmcresource::model_texture_binding {

struct RequiredSlots final {
    std::vector<std::uint32_t> slots;
    std::uint32_t max_slot{};
};

// Pure render-contract validation. No PTX/DDS parsing and no platform state.
[[nodiscard]] bool collect_required_slots(
    const Mesh& mesh,
    std::span<const std::uint32_t> triangle_texture_slots,
    RequiredSlots* out) noexcept;

[[nodiscard]] bool can_attach_texture_companion(
    const Mesh& mesh,
    std::span<const std::uint32_t> triangle_texture_slots) noexcept;

}  // namespace dmcresource::model_texture_binding
