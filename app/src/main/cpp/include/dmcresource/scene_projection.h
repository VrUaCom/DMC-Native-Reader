#pragma once
#include "dmcresource/render_scene.h"

namespace dmcresource {
struct HierarchyEdge final {
    std::uint32_t parent{};
    std::uint32_t child{};
};

struct HierarchyOverlay final {
    std::vector<Vec3> points;
    std::vector<RenderNodeKind> kinds;
    std::vector<HierarchyEdge> edges;
    bool spatial{false};

    [[nodiscard]] bool available() const noexcept {
        return spatial && !points.empty();
    }
};

[[nodiscard]] bool materialize_render_scene(const RenderScene& scene,
                                            Mesh* out) noexcept;

// Material projection stays separate from Mesh ABI. One value per flattened
// triangle, derived from RenderScene::TextureBinding. UINT32_MAX means no
// canonical texture binding for that triangle.
[[nodiscard]] bool materialize_triangle_texture_slots(
    const RenderScene& scene,
    std::vector<std::uint32_t>* out) noexcept;

[[nodiscard]] bool materialize_hierarchy_overlay(const RenderScene& scene,
                                                 HierarchyOverlay* out) noexcept;


}  // namespace dmcresource
