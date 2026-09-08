#pragma once

#include <cstdint>
#include <vector>

#include "dmcresource/mesh.h"
#include "dmcresource/render_scene.h"

namespace dmcresource {

enum class RenderFlag : std::uint32_t {
    Wireframe = 1U << 0U,
    Hierarchy = 1U << 1U,
    Bounds = 1U << 2U,
    SkinDebug = 1U << 3U,
    Normals = 1U << 4U,
    UvLayout = 1U << 5U,
};

using RenderFlags = std::uint32_t;

[[nodiscard]] constexpr RenderFlags render_flag(RenderFlag value) noexcept {
    return static_cast<RenderFlags>(value);
}

[[nodiscard]] constexpr bool has_render_flag(RenderFlags mask,
                                             RenderFlag value) noexcept {
    return (mask & render_flag(value)) != 0U;
}

struct ViewState {
    float yaw_radians{0.65f};
    float pitch_radians{-0.45f};
    float zoom{1.0f};
    bool wireframe{false};
    bool uv_layout{false};
};

struct HierarchyEdge final {
    std::uint32_t parent{};
    std::uint32_t child{};
};

// Format-agnostic spatial projection of RenderScene nodes. It is materialized
// once when a resource opens and reused by the UI renderer. A format adapter
// only needs to publish valid RenderNode world matrices; no format knowledge is
// allowed in the overlay renderer.
struct HierarchyOverlay final {
    std::vector<Vec3> points;
    std::vector<RenderNodeKind> kinds;
    std::vector<HierarchyEdge> edges;
    bool spatial{false};

    [[nodiscard]] bool available() const noexcept {
        return spatial && !points.empty();
    }
};

// Materialize local-space scene primitives into one world-space render mesh
// using the explicit MeshPrimitive -> RenderNode binding. Unbound primitives
// remain in local space. Complete UV0 channels are preserved unchanged because
// world transforms affect geometry, not texture coordinates.
[[nodiscard]] bool materialize_render_scene(const RenderScene& scene,
                                            Mesh* out) noexcept;

// Extract world-space node positions and parent-child edges from RenderScene.
// Returns false only for malformed node/matrix data. A valid but non-spatial
// hierarchy (for example hierarchy metadata without decoded transforms) returns
// true with overlay.available() == false, so the UI can remain evidence-safe.
[[nodiscard]] bool materialize_hierarchy_overlay(const RenderScene& scene,
                                                 HierarchyOverlay* out) noexcept;

// One generic CPU renderer. Normal mode consumes positions/indices; UV Layout
// mode consumes only the neutral UV0 channel + topology and therefore remains
// independent of MOD/SCM parser details.
RgbaImage render_view(const Mesh& mesh, int width, int height,
                      const ViewState& view,
                      const HierarchyOverlay* hierarchy = nullptr);

}  // namespace dmcresource
