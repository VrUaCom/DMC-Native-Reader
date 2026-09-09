#pragma once

#include <cstdint>
#include <span>

#include "dmcresource/mesh.h"
#include "dmcresource/resource_capabilities.h"

namespace dmcresource::spider::black_widow {

// Black Widow owns platform-neutral application/UI decisions. Android/Java
// consumes this typed state but must not reconstruct it from diagnostics.
enum class StateFlag : std::uint64_t {
    TextureCompanionAttachable = 1ULL << 0U,
    TextureCompanionAttached   = 1ULL << 1U,
};

using StateBits = std::uint64_t;

[[nodiscard]] constexpr StateBits state_flag(StateFlag value) noexcept {
    return static_cast<StateBits>(value);
}

[[nodiscard]] constexpr bool has_state(StateBits state,
                                       StateFlag value) noexcept {
    return (state & state_flag(value)) != 0U;
}

struct ModelSessionView final {
    ResourceCapabilities capabilities{};
    bool renderable{};
    const Mesh* render_mesh{};
    std::span<const std::uint32_t> triangle_texture_slots{};
    bool texture_companion_attached{};
};

// Evaluates only platform-neutral state. UINT32_MAX is the neutral sentinel for
// an unbound triangle; no PTX/MOD/SCM format knowledge lives here.
[[nodiscard]] StateBits evaluate_model_session(
    const ModelSessionView& session) noexcept;

}  // namespace dmcresource::spider::black_widow
