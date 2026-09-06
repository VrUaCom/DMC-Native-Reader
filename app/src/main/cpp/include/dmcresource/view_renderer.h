#pragma once

#include <cstdint>
#include <vector>

#include "dmcresource/mesh.h"
#include "dmcresource/render_scene.h"

namespace dmcresource {

struct ViewState {
    float yaw_radians{0.65f};
    float pitch_radians{-0.45f};
    float zoom{1.0f};
    bool wireframe{false};
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
// remain in local space. Call once when opening a static resource and cache the
// result; the rasterizer deliberately does not hide scene materialization per
// frame. Returns false for malformed bindings/matrices or size overflow.
[[nodiscard]] bool materialize_render_scene(const RenderScene& scene,
                                            Mesh* out) noexcept;

// Extract world-space node positions and parent-child edges from RenderScene.
// Returns false only for malformed node/matrix data. A valid but non-spatial
// hierarchy (for example hierarchy metadata without decoded transforms) returns
// true with overlay.available() == false, so the UI can remain evidence-safe.
[[nodiscard]] bool materialize_hierarchy_overlay(const RenderScene& scene,
                                                 HierarchyOverlay* out) noexcept;

RgbaImage render_view(const Mesh& mesh, int width, int height,
                      const ViewState& view,
                      const HierarchyOverlay* hierarchy = nullptr);

}  // namespace dmcresource
