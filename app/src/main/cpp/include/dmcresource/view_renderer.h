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
    Shadows = 1U << 6U,
    Collision = 1U << 7U,
    Room = 1U << 8U,
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
    // Optional stable camera framing source. While a motion plays, the camera
    // is framed from the rest pose so the view does not re-center or re-zoom
    // every frame. Empty: frame from the drawn mesh (previous behaviour).
    std::span<const Vec3> framing_vertices{};
    // Optional floor (plane y = floor_y under the model) and the shadow
    // footprint drawn on it, 3 vertices per triangle (see shadow_hull.h).
    bool floor{false};
    float floor_y{0.0F};
    std::span<const Vec3> floor_shadow{};
    // Optional coloured line pairs drawn over the model (collision debug).
    std::span<const Vec3> overlay_lines{};
    // Texture for triangles without one (neutral_texture.h); lit by a
    // camera light so the form stays readable. nullptr: depth-shaded grey.
    const ImagePreview* fallback_texture{nullptr};
    // Optional room (stage_room.h) drawn around the model instead of the
    // floor: its mesh moved by room_offset, near-plane clipped, perspective-
    // correct, faces turned away from the camera skipped (so the wall between
    // the camera and the model does not hide it).
    const Mesh* room_mesh{nullptr};
    const std::vector<std::uint32_t>* room_texture_slots{nullptr};
    const std::vector<ImagePreview>* room_textures{nullptr};
    const std::vector<std::uint8_t>* room_translucent_triangles{nullptr};
    Vec3 room_offset{};
};

RgbaImage render_uv_map(std::span<const Vec2> coordinates,
    std::span<const std::uint32_t> indices, int width, int height, float zoom);

RgbaImage render_view(const Mesh& mesh, int width, int height,
                      const ViewState& view,
                      const HierarchyOverlay* hierarchy = nullptr,
                      const std::vector<std::uint32_t>* triangle_texture_slots = nullptr,
                      const std::vector<ImagePreview>* textures = nullptr);

// Image-pixel-space projection of hierarchy->points, one entry per point,
// using the exact same camera framing (mesh-centered pinhole, matching
// render_view's own hierarchy marker pass) so a caller can hit-test the
// screen position render_view actually drew each joint marker at -- e.g. to
// show a bone's name/id on hover -- without re-deriving that math itself.
struct HierarchyScreenPoint {
    float x{};
    float y{};
};

[[nodiscard]] std::vector<HierarchyScreenPoint> project_hierarchy_points(
    const Mesh& mesh, const HierarchyOverlay& hierarchy, int width, int height,
    const ViewState& view);

}  // namespace dmcresource
