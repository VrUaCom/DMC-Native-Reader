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
    SmoothTextures = 1U << 9U,  // bilinear texture filtering on models
    Unlit = 1U << 10U,          // no camera light on models
    Preview = 1U << 13U,        // fast frame while the view moves: nearest texels
    // Bits 11-12: background (0 dark, 1 grey, 2 light, 3 black).
};
inline constexpr std::uint32_t kRenderBackgroundShift = 11U;

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
    // Viewer settings: bilinear model textures, no model light, background.
    bool smooth_textures{false};
    bool unlit{false};
    bool fast_preview{false};  // nearest texels everywhere (a frame while moving)
    std::uint8_t background{0U};
    // Gesture camera controls: camera-plane pan in framing radii, a shift of
    // the framing centre (camera follows the model), and the room turned
    // about room_pivot (room coordinates) before room_offset moves it.
    float pan_x{0.0F};
    float pan_y{0.0F};
    Vec3 frame_shift{};
    float room_yaw{0.0F};
    Vec3 room_pivot{};
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

// What lies under an image pixel: the nearest model or visible room surface
// on the view ray, the room point in room coordinates, and the hierarchy
// joint drawn nearest to the pixel (within max_joint_px).
struct ViewPick {
    bool model{false};
    bool room{false};
    bool room_floor{false};  // the room surface hit faces up (a model can stand on it)
    Vec3 room_point{};
    int joint{-1};
    float joint_px{0.0F};
};

[[nodiscard]] ViewPick pick_view(const Mesh& mesh, int width, int height, const ViewState& view,
                                 float x, float y, const HierarchyOverlay* hierarchy = nullptr,
                                 float max_joint_px = 28.0F);

[[nodiscard]] std::vector<HierarchyScreenPoint> project_hierarchy_points(
    const Mesh& mesh, const HierarchyOverlay& hierarchy, int width, int height,
    const ViewState& view);

}  // namespace dmcresource
