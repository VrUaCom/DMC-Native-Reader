#include "dmcresource/model_texture_binding.h"
#include "dmcresource/render_scene.h"

#include <algorithm>
#include <bitset>
#include <cmath>
#include <limits>

namespace dmcresource::model_texture_binding {
namespace {

constexpr std::uint32_t kMaxCompanionTextureSlot = 4095U;
constexpr std::size_t kCompanionTextureSlotCount =
    static_cast<std::size_t>(kMaxCompanionTextureSlot) + 1U;

using SlotSeen = std::bitset<kCompanionTextureSlotCount>;

[[nodiscard]] bool validate_mesh_binding_shape(
        const Mesh& mesh,
        std::span<const std::uint32_t> triangle_texture_slots) noexcept {
    if (!mesh.has_uv0() || mesh.indices.size() < 3U ||
        mesh.indices.size() % 3U != 0U ||
        triangle_texture_slots.size() != mesh.indices.size() / 3U) {
        return false;
    }
    for (const auto index : mesh.indices) {
        if (index >= mesh.vertices.size()) return false;
        const auto& uv = mesh.uv0[index];
        if (!std::isfinite(uv.u) || !std::isfinite(uv.v)) return false;
    }
    return true;
}

[[nodiscard]] bool append_required_slot(std::uint32_t slot,
                                        SlotSeen* seen,
                                        RequiredSlots* out) {
    if (seen == nullptr || out == nullptr || slot == kNoTextureSlot ||
        slot > kMaxCompanionTextureSlot) {
        return false;
    }
    const auto index = static_cast<std::size_t>(slot);
    if (!seen->test(index)) {
        seen->set(index);
        out->slots.push_back(slot);
        out->max_slot = std::max(out->max_slot, slot);
    }
    return true;
}

}  // namespace

bool collect_required_slots(
    const Mesh& mesh,
    std::span<const std::uint32_t> triangle_texture_slots,
    RequiredSlots* out) noexcept {
    if (out == nullptr) return false;
    out->slots.clear();
    out->max_slot = 0U;

    if (!validate_mesh_binding_shape(mesh, triangle_texture_slots)) return false;

    try {
        out->slots.reserve(std::min(
            triangle_texture_slots.size(), kCompanionTextureSlotCount));
        SlotSeen seen;
        for (const auto slot : triangle_texture_slots) {
            if (!append_required_slot(slot, &seen, out)) return false;
        }
    } catch (...) {
        out->slots.clear();
        out->max_slot = 0U;
        return false;
    }

    return !out->slots.empty();
}

bool collect_required_slots(
    const RenderScene& scene,
    std::span<const std::uint32_t> triangle_texture_slots,
    RequiredSlots* out) noexcept {
    if (out == nullptr) return false;
    out->slots.clear();
    out->max_slot = 0U;

    std::size_t expected_triangles = 0U;
    for (const auto& primitive : scene.meshes) {
        if (primitive.mesh.indices.size() % 3U != 0U) return false;
        const auto triangles = primitive.mesh.indices.size() / 3U;
        if (triangles > std::numeric_limits<std::size_t>::max() - expected_triangles) {
            return false;
        }
        expected_triangles += triangles;
    }
    if (expected_triangles == 0U || triangle_texture_slots.size() != expected_triangles) {
        return false;
    }

    try {
        out->slots.reserve(std::min(expected_triangles, kCompanionTextureSlotCount));
        SlotSeen seen;
        std::size_t slot_offset = 0U;
        for (const auto& primitive : scene.meshes) {
            const std::size_t triangles = primitive.mesh.indices.size() / 3U;
            if (triangles == 0U) continue;
            const auto local_slots = triangle_texture_slots.subspan(slot_offset, triangles);
            if (!validate_mesh_binding_shape(primitive.mesh, local_slots)) return false;
            for (const auto slot : local_slots) {
                if (!append_required_slot(slot, &seen, out)) return false;
            }
            slot_offset += triangles;
        }
        if (slot_offset != triangle_texture_slots.size()) return false;
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

bool can_attach_texture_companion(
    const RenderScene& scene,
    std::span<const std::uint32_t> triangle_texture_slots) noexcept {
    RequiredSlots required;
    return collect_required_slots(scene, triangle_texture_slots, &required);
}

}  // namespace dmcresource::model_texture_binding
