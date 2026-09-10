#pragma once

#include <cstdint>
#include <vector>
#include <span>

#include "dmcresource/image_preview.h"
#include "dmcresource/mesh.h"
#include "dmcresource/scene_projection.h"

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

RgbaImage render_uv_map(std::span<const Vec2> coordinates,
    std::span<const std::uint32_t> indices, int width, int height, float zoom);

RgbaImage render_view(const Mesh& mesh, int width, int height,
                      const ViewState& view,
                      const HierarchyOverlay* hierarchy = nullptr,
                      const std::vector<std::uint32_t>* triangle_texture_slots = nullptr,
                      const std::vector<ImagePreview>* textures = nullptr);

}  // namespace dmcresource
