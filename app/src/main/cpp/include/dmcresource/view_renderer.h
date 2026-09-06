#pragma once

#include "dmcresource/mesh.h"
#include "dmcresource/render_scene.h"

namespace dmcresource {

struct ViewState {
    float yaw_radians{0.65f};
    float pitch_radians{-0.45f};
    float zoom{1.0f};
    bool wireframe{false};
};

// Materialize local-space scene primitives into one world-space render mesh
// using the explicit MeshPrimitive -> RenderNode binding. Unbound primitives
// remain in local space. Call once when opening a static resource and cache the
// result; the rasterizer deliberately does not hide scene materialization per
// frame. Returns false for malformed bindings/matrices or size overflow.
[[nodiscard]] bool materialize_render_scene(const RenderScene& scene,
                                            Mesh* out) noexcept;

RgbaImage render_view(const Mesh& mesh, int width, int height,
                      const ViewState& view);

}  // namespace dmcresource
