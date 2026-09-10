#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "dmcresource/mesh.h"
#include "dmcresource/resource_capabilities.h"

namespace dmcresource::spider::black_widow {

// Black Widow owns platform-neutral application/UI decisions. Android/Java
// consumes this typed state but must not reconstruct it from diagnostics or
// raw ResourceCapabilities combinations.
enum class StateFlag : std::uint64_t {
    CanRender                  = 1ULL << 0U,
    CanWireframe               = 1ULL << 1U,
    CanInspect                 = 1ULL << 2U,
    CanShowHierarchy           = 1ULL << 3U,
    HasSkinning                = 1ULL << 4U,
    HasSkinWeights             = 1ULL << 5U,
    HasTextureBindings         = 1ULL << 6U,
    CanPreviewImage            = 1ULL << 7U,
    HasChildResources          = 1ULL << 8U,
    IsText                     = 1ULL << 9U,
    IsContainer                = 1ULL << 10U,
    HasCollision               = 1ULL << 11U,
    HasAdjacency               = 1ULL << 12U,
    HasTransformSelectors      = 1ULL << 13U,
    CanShowUv                  = 1ULL << 14U,
    ChildBrowserMode           = 1ULL << 15U,
    TextureCompanionAttachable = 1ULL << 16U,
    TextureCompanionAttached   = 1ULL << 17U,
    UvMapView                  = 1ULL << 18U,
    CanInspectUv               = 1ULL << 19U,
    CanInspectMeshes           = 1ULL << 20U,
    CanInspectHierarchy        = 1ULL << 21U,
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
    bool hierarchy_available{};
    bool image_preview_available{};
    std::size_t child_resource_count{};
    bool texture_companion_attached{};
    bool uv_map_view{};
    bool uv_data_available{};
    std::size_t object_count{};
    std::size_t hierarchy_node_count{};
};

// Evaluates only platform-neutral session state. UINT32_MAX is the neutral
// sentinel for an unbound triangle; no PTX/MOD/SCM binary-format knowledge
// lives here.
[[nodiscard]] StateBits evaluate_model_session(
    const ModelSessionView& session) noexcept;

}  // namespace dmcresource::spider::black_widow
