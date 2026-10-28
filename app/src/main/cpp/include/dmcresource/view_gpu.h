#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "dmcresource/image_preview.h"
#include "dmcresource/mesh.h"
#include "dmcresource/render_scene.h"
#include "dmcresource/camera_post.h"
#include "dmcresource/dmc3_shaders.h"

// GPU view rendering: render_view's triangle passes on the graphics chip.
//
// Core keeps the render contract. render_view builds a backend-neutral
// GpuViewFrame with the same rules as the software rasteriser (camera
// framing, which triangle is lit / textured / additive, room soft alpha and
// alpha-tested cut-outs, scroll offsets, floor, shadow footprint, effect
// quads); a platform backend draws it into an offscreen target and reads the
// pixels back (Android: OpenGL ES 3 through EGL, android/gles_view_backend.cpp);
// Core then draws the line overlays (attack shapes, HITS, bones) on the
// result exactly as before. Wireframe and UV views, and any frame the backend
// cannot draw, stay on the software rasteriser.
namespace dmcresource {

// One de-indexed vertex. Model: light = camera light term (vertex_light),
// normal unused. Room: normal = the triangle's summed vertex normals (zero
// when the room has none), light unused.
struct GpuVertex final {
    float x{}, y{}, z{};
    float u{}, v{};
    std::array<std::uint8_t, 4> rgba{128U, 128U, 128U, 128U};  // COLOR0, 0x80 = 1
    float nx{}, ny{}, nz{};
    float light{};
};

enum class GpuBatchKind : std::uint8_t {
    model,              // alpha blend, writes depth, texel alpha 0 discarded
    model_additive,     // GS ALPHA 2: dst + src * a, no depth write
    model_subtractive,  // GS ALPHA 3: dst - src * a, no depth write
    room,               // opaque texels (pass 1) / soft texels when translucent (pass 2)
    room_additive,      // light shafts: dst + src * light * a
    room_subtractive,
};

struct GpuBatch final {
    const ImagePreview* texture{};  // null: untextured (model grey / room 0x80)
    std::uint32_t first{};          // first vertex
    std::uint32_t count{};          // vertices (3 per triangle)
    GpuBatchKind kind{GpuBatchKind::model};
    bool colored{};                 // texel x COLOR0 / 0x80
    bool translucent{};             // room: soft-alpha texture
    // Light = base + gain * term; term = vertex light (model) or the facing
    // |cos| of the triangle normal and the view ray (room; 1 without normals).
    float light_base{1.0F};
    float light_gain{0.0F};
    std::uint32_t scroll_slot{kNoTextureSlot};  // room texture slot (scrolls)
    // The object's material (game_material.h): point sampling, and the
    // part alpha the DMC3 shaders draw with (128/255, or 1 above 0x80).
    bool nearest{};
    float part_alpha{128.0F / 255.0F};
};

struct GpuGeometry final {
    std::vector<GpuVertex> vertices;
    std::vector<GpuBatch> batches;
    // Bounding sphere of the vertices (the room's far plane).
    Vec3 center{};
    float radius{};
};

// Identity of a room's geometry; the backend rebuilds its cached buffers
// only when it changes.
struct GpuRoomKey final {
    const void* mesh{};
    const void* vertices{};
    const void* indices{};
    const void* slots{};
    const void* textures{};
    const void* translucent{};
    const void* fallback{};
    std::size_t vertex_count{};
    std::size_t index_count{};
    std::size_t texture_count{};
    std::uint64_t content{};
    std::uint64_t revision{};
    [[nodiscard]] bool operator==(const GpuRoomKey&) const = default;
};

// Effect quads stay in image space, as the software pass rasterises them
// (affine, x/y in pixels, z = camera depth relative to the framing centre).
struct GpuScreenVertex final {
    float x{}, y{}, z{};
    float u{}, v{};
    std::array<float, 4> rgba{255.0F, 255.0F, 255.0F, 255.0F};
};

struct GpuScreenQuad final {
    std::array<GpuScreenVertex, 4> vertices{};
    const ImagePreview* texture{};  // null: solid tint
    bool additive{};
};

// Picture quality of the GPU pass (Settings -> Graphics). The software
// rasteriser has none of these.
struct GpuViewOptions final {
    int msaa_samples{4};  // 0 (off), 2, 4, 8, 16; capped by the chip and target memory
    bool mipmaps{true};   // mipmapped sampling (smooth: trilinear, pixel: nearest mip)
    int anisotropy{8};    // 1 (off) .. 16; smooth textures only, capped by the chip
};

struct GpuCapabilities final {
    int max_samples{};
    float max_anisotropy{};  // 0: no anisotropic filtering
};

struct GpuViewFrame final {
    GpuViewOptions options{};
    int width{};
    int height{};
    std::array<std::uint8_t, 3> background{};
    // Camera space c = view * p (column-major, p in model or room
    // coordinates); clip = (projection[0] * c.x, projection[1] * c.y,
    // projection[2] * c.z + projection[3], c.z). Image row 0 is window row 0.
    std::array<float, 16> model_view{};
    std::array<float, 16> room_view{};
    std::array<float, 4> projection{};
    float near_z{};
    float far_z{};
    float camera_distance{};  // camera z of the framing centre
    float radius{};           // framing radius (model grey shade)
    bool smooth_model{};
    bool smooth_room{true};

    GpuGeometry model;  // rebuilt every frame (posed vertices)

    // Room: built by build_room() only when room_key differs from the
    // backend's cached one.
    bool room{};
    GpuRoomKey room_key{};
    std::function<GpuGeometry()> build_room;
    struct Scroll final {
        std::uint32_t slot{};
        float u{}, v{};
    };
    std::vector<Scroll> room_scrolls;  // offsets this frame

    // Plain floor (two triangles, model coordinates) and the shadow
    // footprint on it (3 vertices per triangle).
    std::vector<Vec3> floor;
    std::array<std::uint8_t, 3> floor_rgb{64U, 67U, 76U};
    std::vector<Vec3> shadow;

    std::vector<GpuScreenQuad> effects;

    // Depth fog (ViewState::Fog): the surfaces blend toward `near_colour`
    // (rgb 0..1, alpha) at camera distance `start` and toward `far_colour`
    // at `end`; effects are not fogged.
    struct Fog final {
        bool enabled{};
        float start{};
        float end{};
        std::array<float, 4> near_colour{};
        std::array<float, 4> far_colour{};
    };
    Fog fog{};

    // The photo camera's development (null: the frame as drawn). The
    // backend sets post->applied when it developed the frame.
    const CameraPost* post{};

    // A cutscene's fade over the drawn scene (rgb 0..255, amount 0..1),
    // laid before the development.
    std::array<float, 4> fade{};

    // The lines, crosses and labels over the finished frame (after the
    // development, no depth test), as the CPU's overlays draw them: image
    // pixels, x right and y down. Solid triangles (rgba 0..255, 3 a
    // triangle), and labels with their own small picture (the 5x7 text and
    // its dark rim, transparent around it) at their top-left corner.
    struct Overlay final {
        std::vector<GpuScreenVertex> triangles;
        struct Label final {
            float x{};
            float y{};
            ImagePreview image;
        };
        std::vector<Label> labels;
        [[nodiscard]] bool empty() const noexcept { return triangles.empty() && labels.empty(); }
    };
    Overlay overlay;

    // The DMC3 shaders (dmc3_shaders.h, dmc3_draw): when enabled, a batch
    // the game has a shader for is drawn with it instead of the Reader's
    // own; everything else keeps the Reader's shaders.
    struct Dmc3 final {
        bool enabled{};
        bool fog{};                          // the fog variants
        std::array<float, 3> fog_colour{};   // rgb 0..1
        // Fog amount (1 none, 0 all) = clamp(x + y / camera distance): the
        // PS2 curve, through the Reader fog's amounts at its start and end.
        std::array<float, 2> fog_coeffs{1.0F, 0.0F};
        float model_fog{1.0F};               // the model's amount, at the framing centre
        // The Reader's camera light (two-sided) in model coordinates, for
        // model_lit's light rig.
        std::array<float, 3> light_direction{};
        // The stage's lights (ViewState::stage_lights, when
        // game_lights_enabled): the four stage_lit draws with (room
        // coordinates; colours per unit of facing), and up to three for the
        // model's rig (unit directions in model coordinates; 128 = 1).
        int stage_light_count{};
        std::array<float, 4> stage_light_x{}, stage_light_y{}, stage_light_z{}, stage_light_falloff{};
        std::array<float, 16> stage_light_colour{};
        int model_light_count{};
        std::array<float, 9> model_light_directions{};
        std::array<float, 9> model_light_colours{};
    };
    Dmc3 dmc3{};

    // Show the frame on the backend's window (set_window) instead of
    // reading it back; the backend sets `presented` when it did, and the
    // image is then left without pixels.
    bool present{};
    mutable bool presented{};
};

// The passes of the frame a mesh batch is drawn in.
enum class GpuPass : std::uint8_t {
    room_opaque,  // 1. opaque room texels
    model,        // 3. the model
    room_soft,    // 4. soft room texels
    room_layered, // 4. room light shafts (added / subtracted)
};

// How a batch is drawn with the DMC3 shaders: the pair, the alpha test and
// the light rig. vertex == none: with the Reader's own shaders. The rules are
// Core's (dmc3_draw); the backend sets the uniforms CONVENTIONS.md names.
struct GpuDmc3Draw final {
    dmc3::Shader vertex{dmc3::Shader::none};
    dmc3::Shader fragment{dmc3::Shader::none};
    dmc3::AlphaFunc alpha_func{dmc3::AlphaFunc::off};
    float alpha_ref{};
    std::array<float, 3> fog_colour{};  // black for added / subtracted surfaces
    float fog_amount{1.0F};             // model: the part's amount
    // Model: u_light_directions / u_light_colours (column-major), u_part_colour
    // rgb and u_part_alpha. Stage: u_ambient, no point lights.
    std::array<float, 16> light_directions{};
    std::array<float, 16> light_colours{};
    float part_colour{0.5F};
    float part_alpha{128.0F / 255.0F};
    float ambient{1.0F};
    // Stage: u_light_x / _y / _z / _falloff and u_light_colour[4].
    std::array<float, 4> light_x{}, light_y{}, light_z{}, light_falloff{};
    std::array<float, 16> light_colour{};
    [[nodiscard]] bool drawn() const noexcept { return vertex != dmc3::Shader::none; }
};
[[nodiscard]] GpuDmc3Draw dmc3_draw(const GpuBatch& batch, GpuPass pass, const GpuViewFrame& frame) noexcept;

class GpuViewBackend {
public:
    virtual ~GpuViewBackend() = default;
    // Draws the frame into `image` (width x height RGBA8, rows top-down,
    // alpha 255; sized by the caller, except for a frame that may be shown
    // on the window: the backend sizes it when it reads it back). False:
    // the frame is drawn on the CPU instead.
    [[nodiscard]] virtual bool draw(const GpuViewFrame& frame, RgbaImage& image) = 0;
    // "OpenGL ES 3.2 / Adreno (TM) ...", empty when unavailable.
    [[nodiscard]] virtual std::string describe() = 0;
    // Limits of the chip (zeros when unavailable).
    [[nodiscard]] virtual GpuCapabilities capabilities() { return {}; }
    // The view's window frames are shown on (Android: an ANativeWindow*
    // the backend now holds a reference to; nullptr: none). Returns once
    // the backend has let go of the one before.
    virtual void set_window(void* window) { (void)window; }
};

// The process-wide backend (nullptr: software only) and the user switch.
void set_gpu_view_backend(GpuViewBackend* backend) noexcept;
[[nodiscard]] GpuViewBackend* gpu_view_backend() noexcept;
void set_gpu_view_enabled(bool enabled) noexcept;
[[nodiscard]] bool gpu_view_enabled() noexcept;
void set_gpu_view_options(const GpuViewOptions& options) noexcept;
// Frames of the view go straight to its window when they can (on by default).
void set_gpu_present_enabled(bool enabled) noexcept;
[[nodiscard]] bool gpu_present_enabled() noexcept;
[[nodiscard]] GpuViewOptions gpu_view_options() noexcept;
// Draw with the DMC3 shaders where the game has one (on by default).
void set_dmc3_shaders_enabled(bool enabled) noexcept;
[[nodiscard]] bool dmc3_shaders_enabled() noexcept;
// Light the DMC3 shaders' stage and models with the stage's light table
// (experimental: that LIG2 feeds the game's lights is not proven; off by
// default).
void set_game_lights_enabled(bool enabled) noexcept;
[[nodiscard]] bool game_lights_enabled() noexcept;

struct GpuViewStats final {
    std::uint64_t gpu_frames{};
    std::uint64_t cpu_frames{};      // software frames (any reason)
    std::uint64_t gpu_failures{};    // backend refused a frame
    std::uint64_t presented_frames{};  // GPU frames shown straight on the window
};

// What the backend did with the DMC3 shaders: how many compiled when it
// started, and the batches of the last frame drawn with them.
struct GpuShaderStats final {
    int compiled{};
    int failed{};
    std::string first_failure;  // "name: log"
    std::uint64_t dmc3_batches{};    // last frame
    std::uint64_t reader_batches{};  // last frame, with the Reader's shaders
    std::vector<dmc3::Shader> used;  // last frame, each shader once
};
void set_gpu_shader_stats(GpuShaderStats stats);
[[nodiscard]] GpuShaderStats gpu_shader_stats();
// "DMC3 shaders: 67 of 67 compiled; last frame: 24 batches with them
// (effect_model_lit.vert = dmc3_efm, ...), 21 with the Reader's" (Info).
[[nodiscard]] std::string dmc3_shader_report();
[[nodiscard]] GpuViewStats gpu_view_stats() noexcept;

// What draws the next frame: "GPU: <backend>" or "CPU (software)".
[[nodiscard]] std::string view_renderer_description();

}  // namespace dmcresource
