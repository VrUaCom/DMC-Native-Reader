#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "dmcresource/image_preview.h"
#include "dmcresource/mesh.h"
#include "dmcresource/render_scene.h"

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

struct GpuViewFrame final {
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
};

class GpuViewBackend {
public:
    virtual ~GpuViewBackend() = default;
    // Draws the frame into `image` (width x height RGBA8, rows top-down,
    // alpha 255). False: the frame is drawn on the CPU instead.
    [[nodiscard]] virtual bool draw(const GpuViewFrame& frame, RgbaImage& image) = 0;
    // "OpenGL ES 3.2 / Adreno (TM) ...", empty when unavailable.
    [[nodiscard]] virtual std::string describe() = 0;
};

// The process-wide backend (nullptr: software only) and the user switch.
void set_gpu_view_backend(GpuViewBackend* backend) noexcept;
[[nodiscard]] GpuViewBackend* gpu_view_backend() noexcept;
void set_gpu_view_enabled(bool enabled) noexcept;
[[nodiscard]] bool gpu_view_enabled() noexcept;

struct GpuViewStats final {
    std::uint64_t gpu_frames{};
    std::uint64_t cpu_frames{};      // software frames (any reason)
    std::uint64_t gpu_failures{};    // backend refused a frame
};
[[nodiscard]] GpuViewStats gpu_view_stats() noexcept;

// What draws the next frame: "GPU: <backend>" or "CPU (software)".
[[nodiscard]] std::string view_renderer_description();

}  // namespace dmcresource
