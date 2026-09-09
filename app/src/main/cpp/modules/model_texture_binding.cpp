#include "dmcresource/model_texture_binding.h"

#include <algorithm>
#include <limits>

namespace dmcresource::model_texture_binding {
namespace {

constexpr std::uint32_t kNoTextureSlot =
    std::numeric_limits<std::uint32_t>::max();
constexpr std::uint32_t kMaxCompanionTextureSlot = 4095U;

}  // namespace

bool collect_required_slots(
    const Mesh& mesh,
    std::span<const std::uint32_t> triangle_texture_slots,
    RequiredSlots* out) noexcept {
    if (out == nullptr) return false;
    out->slots.clear();
    out->max_slot = 0U;

    if (!mesh.has_uv0() || mesh.indices.size() < 3U ||
        mesh.indices.size() % 3U != 0U ||
        triangle_texture_slots.size() != mesh.indices.size() / 3U) {
        return false;
    }

    try {
        for (const auto slot : triangle_texture_slots) {
            if (slot == kNoTextureSlot) continue;
            if (slot > kMaxCompanionTextureSlot) return false;
            if (std::find(out->slots.begin(), out->slots.end(), slot) ==
                out->slots.end()) {
                out->slots.push_back(slot);
                out->max_slot = std::max(out->max_slot, slot);
            }
        }
    } catch (...) {
        out->slots.clear();
        out->max_slot = 0U;
        return false;
    }

    return !out->slots.empty();
}

bool can_attach_texture_companion(
    const Mesh& mesh,
    std::span<const std::uint32_t> triangle_texture_slots) noexcept {
    RequiredSlots required;
    return collect_required_slots(mesh, triangle_texture_slots, &required);
}

}  // namespace dmcresource::model_texture_binding
