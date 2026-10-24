#pragma once

#include <string>
#include <optional>
#include <array>
#include <cstdint>
#include <vector>

#include "dmcresource/camera_post.h"
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
    // HITS room/environment collision overlay. This is deliberately separate
    // from Collision (character attack shapes on body bones).
    RoomCollision = 1U << 14U,
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

// A coloured line and a text label in room coordinates, drawn over the frame
// like the room collision (stage configuration markers use them).
struct OverlayLine final {
    Vec3 a{};
    Vec3 b{};
    std::array<std::uint8_t, 3> color{255U, 255U, 255U};
    std::uint8_t width{1U};  // multiples of the collision line width
};
struct OverlayLabel final {
    Vec3 at{};
    std::string text;
    std::array<std::uint8_t, 3> color{255U, 255U, 255U};
};

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
    std::span<const Vec3> room_collision_lines{};
    // Colour class of each line pair of room_collision_lines (collision_color_class).
    std::span<const std::uint8_t> room_collision_kinds{};
    // Stage configuration markers in room coordinates (stage_markers.h).
    std::span<const OverlayLine> marker_lines{};
    std::span<const OverlayLabel> marker_labels{};
    // Line widths in image pixels: meshes (wireframe, room wire, bones) and
    // collisions (HITS, attack shapes).
    // Opacity of the room's wireframe lines when the room is only the backdrop
    // of a model (0 hides them, 1 is opaque).
    float room_wire_opacity{0.45F};
    // Texture scrolls of the room (texture index, rate per game frame) and the
    // animation clock in game frames (60 per second; 0 = still).
    struct RoomScroll final {
        std::uint32_t texture{};
        float u_per_frame{};
        float v_per_frame{};
    };
    std::span<const RoomScroll> room_scrolls{};
    float room_time{0.0F};
    // Depth fog (a cutscene's GFG screen effect, a stage configuration's SEF
    // fog): the drawn surfaces blend toward `near_colour` with `near_alpha`
    // at camera distance `start` and toward `far_colour` with `far_alpha`
    // at `end` (alphas 0..1, linear between, held beyond). Effects draw
    // over it unfogged.
    struct Fog final {
        bool enabled{false};
        float start{};
        float end{};
        std::array<std::uint8_t, 3> near_colour{};
        std::array<std::uint8_t, 3> far_colour{};
        float near_alpha{};
        float far_alpha{};
    };
    Fog fog{};
    // The camera turned about its own axis (radians; positive clockwise,
    // the picture turning the other way): the photo camera's roll.
    float roll_radians{};
    // When set, filled with each pixel's distance from the camera (infinity
    // where nothing was drawn): the photo camera's depth of field reads it.
    std::vector<float>* depth_out{};
    // The photo camera's development for a GPU frame (null: none).
    const CameraPost* post{};
    // A cutscene's fade over the drawn scene, before the photo camera
    // develops it: the colour (0..255) and how much of it covers the
    // picture (0: none).
    std::array<float, 4> fade{};
    // Show the frame on the view's window (the GPU backend's, view_gpu.h)
    // instead of handing its pixels back, when nothing is drawn over it on
    // the CPU. A shown frame comes back with its size and no pixels.
    bool present{};
    // One tile of a picture larger than a render (the photo camera's 124 MP
    // and more): the focal length of the whole picture's short side, the
    // whole picture's centre relative to this tile's (pixels), its height.
    struct Tile {
        bool on{};
        float focal_side{};
        float principal_x{};
        float principal_y{};
        int full_height{};
    } tile{};
    int mesh_line_px{1};
    int collision_line_px{1};
    // Resource-backed EXE effect presentation. The runtime supplies explicit
    // world transforms and atlas UVs; the renderer does not infer an effect
    // from an MOT name or from a body joint.
    struct EffectSprite final {
        Matrix4 world{};
        const ImagePreview* texture{};
        float width{};
        float height{};
        float u0{};
        float v0{};
        float u1{1.0F};
        float v1{1.0F};
        // CEffect mode 1 (0x1402E5D00): camera-plane extents around the world
        // anchor, already scaled to world units: x in [left, right], y in
        // [bottom, top]. When unset the centred width/height is used.
        bool extents{};
        float left{}, right{}, bottom{}, top{};
        // CEffect mode 2 (0x1402E69E0): an oriented quad whose world corners
        // are resolved by the caller (order: -B, -B+U, -B+U+V, -B+V).
        bool oriented{};
        std::array<Vec3, 4> corners{};
        // Vertex colour of particle quads (P records): texel * tint / 255,
        // alpha likewise. Additive adds the tinted colour weighted by alpha.
        std::array<std::uint8_t, 4> tint{255U, 255U, 255U, 255U};
        // Per-corner colours (particle quads: one RGBA per vertex, Gouraud);
        // when set they replace `tint`.
        bool per_vertex{false};
        std::array<std::array<std::uint8_t, 4>, 4> corner_tint{};
        // A line from corners[0] to corners[1] (CPtclLine01), drawn a few
        // pixels wide in screen space; colours blend along it.
        bool line{false};
        bool additive{false};
        // Untextured polygon (CPtclPoly00 / Line records): texture is null and
        // the tint is the whole colour.
        bool solid{false};
    };
    std::span<const EffectSprite> effect_sprites{};
    // Texture for triangles without one (neutral_texture.h); lit by a
    // camera light so the form stays readable. nullptr: depth-shaded grey.
    const ImagePreview* fallback_texture{nullptr};
    // Optional room (stage_room.h) drawn around the model instead of the
    // floor: its mesh moved by room_offset, near-plane clipped, perspective-
    // correct, faces turned away from the camera skipped (so the wall between
    // the camera and the model does not hide it).
    const Mesh* room_mesh{nullptr};
    // Changes when room_mesh changes in place (stage_room::Room::revision).
    std::uint64_t room_revision{};
    const std::vector<std::uint32_t>* room_texture_slots{nullptr};
    const std::vector<ImagePreview>* room_textures{nullptr};
    const std::vector<std::uint8_t>* room_translucent_triangles{nullptr};
    Vec3 room_offset{};
    // The room is the subject of the view (a stage scene): its wireframe is
    // drawn at full strength, not as a dim backdrop.
    bool room_wire_main{false};
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
    // Dolly: the camera moves along its view axis by this fraction of the
    // framing distance (>0 toward the orbit centre, <0 away; 1 = at it).
    float dolly{0.0F};
    Vec3 frame_shift{};
    float room_yaw{0.0F};
    Vec3 room_pivot{};
    // Fly camera (stages, collision views, a cutscene's camera): the camera
    // stands at fly_eye and looks along yaw / pitch, turning about itself
    // instead of orbiting the framed centre. Pan and dolly move the eye, as
    // they would move it about content at fly_focus ahead (0: the framing
    // distance); the follow shift does not apply.
    bool fly{false};
    Vec3 fly_eye{};
    float fly_focus{0.0F};
    // Orbit about this point instead of the framed centre (left by a flight,
    // camera_control.h); pan and dolly still apply.
    bool has_orbit_pivot{false};
    Vec3 orbit_pivot{};
};

// World directions of the view camera at yaw / pitch (the same mirrored
// camera as render_view): forward into the screen, right = screen right,
// up = screen up.
struct CameraBasis final {
    Vec3 forward{};
    Vec3 right{};
    Vec3 up{};
};
[[nodiscard]] CameraBasis camera_basis(float yaw_radians, float pitch_radians) noexcept;

// World position of the camera render_view uses for this view (orbit or fly);
// switching a view to fly with this eye keeps the picture unchanged.
[[nodiscard]] Vec3 view_camera_eye(const Mesh& mesh, int width, int height, const ViewState& view);

// One fly step: `forward` along the view direction (pitch included, so
// flying while looking up climbs), `strafe` along screen right, `rise`
// straight up; all in world units.
[[nodiscard]] Vec3 fly_move(const Vec3& eye, float yaw_radians, float pitch_radians, float forward,
                            float strafe, float rise) noexcept;

// Colour of a HITS record kind in the collision overlay and its legend.
// The colour of a HITS record, from its flags only, so a kind has the same
// colour in every file: a record with category bits (flags >> 16: the
// invisible walls, 0x1000 / 0x1800 / 0x1806 / 0x186E in st000..st006) is
// coloured by its category, the others by their type byte (flags & 0xFF:
// 0x00..0x0C seen). Classes 0..15 are type bytes (15: 0x0F and above), 16..19
// the known categories, 20..27 any other category (by a fixed hash).
inline constexpr std::uint8_t kCollisionClassCount = 28U;
[[nodiscard]] std::uint8_t collision_color_class(std::uint32_t flags) noexcept;
[[nodiscard]] std::array<std::uint8_t, 3> collision_class_color(std::uint8_t color_class) noexcept;
// What a class stands for ("type 0x01", "category 0x1806", ...).
[[nodiscard]] std::string collision_class_name(std::uint8_t color_class);
// The colour of a character's attack collision shapes (hitboxes).
inline constexpr std::array<std::uint8_t, 3> kAttackCollisionColor{255U, 90U, 60U};

// Framing distance (model units) of the camera at dolly 0 for these vertices.
[[nodiscard]] float framing_camera_distance(std::span<const Vec3> vertices) noexcept;

// A UV layout in the proportions of the texture it was painted for
// (texture_width x texture_height texels; 0 x 0 draws it square), framed
// for viewing: the layout's extent fitted with a margin, scaled by `zoom`.
RgbaImage render_uv_map(std::span<const Vec2> coordinates,
    std::span<const std::uint32_t> indices, int width, int height, float zoom,
    std::uint32_t texture_width = 0U, std::uint32_t texture_height = 0U);
// The same layout framed for a PNG: the 0..1 texture rectangle fills the
// image exactly (v down, as the texture is sampled), so it overlays the texture.
RgbaImage render_uv_map_export(std::span<const Vec2> coordinates,
    std::span<const std::uint32_t> indices, int width, int height,
    std::uint32_t texture_width, std::uint32_t texture_height);

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

// Image positions of room points in that view (room placement applied);
// nullopt for a point behind the camera.
[[nodiscard]] std::vector<std::optional<HierarchyScreenPoint>> project_room_points(
    const Mesh& mesh, int width, int height, const ViewState& view, std::span<const Vec3> room_points);

[[nodiscard]] std::vector<HierarchyScreenPoint> project_hierarchy_points(
    const Mesh& mesh, const HierarchyOverlay& hierarchy, int width, int height,
    const ViewState& view);

}  // namespace dmcresource
