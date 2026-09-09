#include "dmcresource/spider/black_widow.h"

#include <algorithm>
#include <limits>

namespace dmcresource::spider::black_widow {
namespace {

constexpr std::uint32_t kNoTextureSlot =
    std::numeric_limits<std::uint32_t>::max();

[[nodiscard]] bool has_usable_texture_slot_mapping(
    const Mesh& mesh,
    std::span<const std::uint32_t> slots) noexcept {
    if (mesh.indices.size() < 3U || mesh.indices.size() % 3U != 0U) return false;
    if (slots.size() != mesh.indices.size() / 3U) return false;
    return std::any_of(slots.begin(), slots.end(), [](std::uint32_t slot) {
        return slot != kNoTextureSlot;
    });
}

}  // namespace

StateBits evaluate_model_session(const ModelSessionView& session) noexcept {
    StateBits state = 0U;
    if (!session.renderable || session.render_mesh == nullptr) return state;

    const bool capability_gate =
        has_capability(session.capabilities, ResourceCapability::Geometry) &&
        has_capability(session.capabilities, ResourceCapability::UvCoordinates) &&
        has_capability(session.capabilities, ResourceCapability::TextureBinding);

    const bool attachable = capability_gate &&
        session.render_mesh->has_uv0() &&
        has_usable_texture_slot_mapping(
            *session.render_mesh, session.triangle_texture_slots);

    if (!attachable) return state;
    state |= state_flag(StateFlag::TextureCompanionAttachable);
    if (session.texture_companion_attached) {
        state |= state_flag(StateFlag::TextureCompanionAttached);
    }
    return state;
}

}  // namespace dmcresource::spider::black_widow
