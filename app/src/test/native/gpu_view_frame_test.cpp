// GPU view frame contract (view_gpu.h) with a recording backend: render_view
// hands the triangle passes to the backend with the software rasteriser's
// camera and rules, draws the line overlays on the result, and falls back to
// the software picture whenever the backend refuses a frame.
#include <cassert>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#include "dmcresource/scene_projection.h"
#include "dmcresource/view_gpu.h"
#include "dmcresource/view_renderer.h"

using namespace dmcresource;

namespace {

struct Recording final {
    int calls{};
    GpuViewOptions options{};
    std::vector<GpuBatch> model_batches;
    std::vector<GpuVertex> model_vertices;
    std::array<float, 16> model_view{};
    std::array<float, 4> projection{};
    float near_z{}, far_z{};
    bool room{};
    GpuRoomKey room_key{};
    GpuGeometry room_geometry;
    std::vector<Vec3> floor;
    std::vector<Vec3> shadow;
};

class RecordingBackend final : public GpuViewBackend {
public:
    bool accept{true};
    Recording last;

    bool draw(const GpuViewFrame& frame, RgbaImage& image) override {
        ++last.calls;
        last.options = frame.options;
        last.model_batches = frame.model.batches;
        last.model_vertices = frame.model.vertices;
        last.model_view = frame.model_view;
        last.projection = frame.projection;
        last.near_z = frame.near_z;
        last.far_z = frame.far_z;
        last.room = frame.room;
        last.room_key = frame.room_key;
        last.floor = frame.floor;
        last.shadow = frame.shadow;
        if (frame.room) last.room_geometry = frame.build_room();
        if (!accept) return false;
        for (std::size_t i = 0U; i < image.pixels.size(); i += 4U) {
            image.pixels[i + 0U] = frame.background[0];
            image.pixels[i + 1U] = frame.background[1];
            image.pixels[i + 2U] = frame.background[2];
            image.pixels[i + 3U] = 255U;
        }
        return true;
    }
    std::string describe() override { return "recording"; }
};

Mesh quad_mesh() {
    Mesh mesh;
    mesh.vertices = {{-1.0F, 0.0F, 0.0F}, {1.0F, 0.0F, 0.0F}, {1.0F, 2.0F, 0.0F}, {-1.0F, 2.0F, 0.0F}};
    mesh.uv0 = {{0.0F, 0.0F}, {1.0F, 0.0F}, {1.0F, 1.0F}, {0.0F, 1.0F}};
    mesh.indices = {0U, 1U, 2U, 0U, 2U, 3U};
    mesh.blend0 = {0U, 0U, 0U, 0U};
    return mesh;
}

ImagePreview checker() {
    ImagePreview image;
    image.width = 2U;
    image.height = 2U;
    image.rgba8 = {200U, 40U, 40U, 255U, 40U, 200U, 40U, 255U, 40U, 40U, 200U, 255U, 220U, 220U, 220U, 255U};
    return image;
}

std::array<float, 2> project(const Recording& r, const Vec3& p, int width, int height) {
    const auto& m = r.model_view;
    const float cx = m[0] * p.x + m[4] * p.y + m[8] * p.z + m[12];
    const float cy = m[1] * p.x + m[5] * p.y + m[9] * p.z + m[13];
    const float cz = m[2] * p.x + m[6] * p.y + m[10] * p.z + m[14];
    return {(r.projection[0] * cx / cz + 1.0F) * 0.5F * static_cast<float>(width),
            (r.projection[1] * cy / cz + 1.0F) * 0.5F * static_cast<float>(height)};
}

}  // namespace

int main() {
    RecordingBackend backend;
    set_gpu_view_backend(&backend);
    set_gpu_view_enabled(true);
    assert(view_renderer_description() == "GPU: recording");

    auto mesh = quad_mesh();
    const std::vector<ImagePreview> textures{checker()};
    const std::vector<std::uint32_t> slots{0U, 0U};
    ViewState view;
    view.floor = true;
    view.floor_y = 0.0F;
    const std::vector<Vec3> shadow{{-1.0F, 0.0F, -1.0F}, {1.0F, 0.0F, -1.0F}, {0.0F, 0.0F, 1.0F}};
    view.floor_shadow = shadow;
    constexpr int W = 320, H = 240;

    // 1. One lit, opaque batch; floor and shadow handed over.
    {
        const auto image = render_view(mesh, W, H, view, nullptr, &slots, &textures);
        assert(image.width == W && image.height == H);
        assert(backend.last.calls == 1);
        assert(backend.last.model_batches.size() == 1U);
        const auto& b = backend.last.model_batches[0];
        assert(b.kind == GpuBatchKind::model && b.count == 6U && b.texture == &textures[0]);
        assert(std::fabs(b.light_base - 0.72F) < 1.0e-6F && std::fabs(b.light_gain - 0.42F) < 1.0e-6F);
        assert(backend.last.floor.size() == 6U && backend.last.shadow.size() == 3U);
        assert(!backend.last.room);
    }

    // 2. Additive triangle (GS ALPHA 2 on its first vertex): a batch of its
    //    own after the opaque one, unlit.
    mesh.blend0 = {0U, 0U, 0U, 2U};
    mesh.indices = {0U, 1U, 2U, 3U, 0U, 2U};
    {
        (void)render_view(mesh, W, H, view, nullptr, &slots, &textures);
        assert(backend.last.model_batches.size() == 2U);
        assert(backend.last.model_batches[0].kind == GpuBatchKind::model);
        assert(backend.last.model_batches[1].kind == GpuBatchKind::model_additive);
        assert(backend.last.model_batches[1].first == 3U && backend.last.model_batches[1].count == 3U);
        assert(backend.last.model_batches[1].light_gain == 0.0F && backend.last.model_batches[1].light_base == 1.0F);
    }

    // 3. Camera: the frame's matrices put a point where the software path
    //    draws its joint marker; every vertex lies between near and far.
    {
        HierarchyOverlay hierarchy;
        hierarchy.spatial = true;
        hierarchy.points = {{0.0F, 1.0F, 0.0F}, {1.0F, 2.0F, 0.0F}, {-1.0F, 0.5F, 0.3F}};
        hierarchy.kinds.assign(3U, RenderNodeKind{});
        hierarchy.edges = {{0U, 1U}, {0U, 2U}};
        ViewState moved = view;
        moved.yaw_radians = 1.1F;
        moved.pitch_radians = -0.3F;
        moved.zoom = 1.7F;
        moved.pan_x = 0.2F;
        moved.pan_y = -0.1F;
        moved.dolly = 0.25F;
        const auto image = render_view(mesh, W, H, moved, &hierarchy, &slots, &textures);
        const auto expected = project_hierarchy_points(mesh, hierarchy, W, H, moved);
        assert(expected.size() == 3U);
        for (std::size_t i = 0U; i < 3U; ++i) {
            const auto got = project(backend.last, hierarchy.points[i], W, H);
            assert(std::fabs(got[0] - expected[i].x) < 0.01F);
            assert(std::fabs(got[1] - expected[i].y) < 0.01F);
        }
        const auto& m = backend.last.model_view;
        for (const auto& v : mesh.vertices) {
            const float cz = m[2] * v.x + m[6] * v.y + m[10] * v.z + m[14];
            assert(cz > backend.last.near_z && cz < backend.last.far_z);
        }
        // The bones are drawn over the backend's picture (it only cleared).
        std::size_t white = 0U;
        for (std::size_t i = 0U; i < image.pixels.size(); i += 4U) {
            white += image.pixels[i] == 255U && image.pixels[i + 1U] == 255U && image.pixels[i + 2U] == 255U;
        }
        assert(white > 20U);
    }

    // 4. A refused frame is the software picture, byte for byte.
    {
        backend.accept = false;
        const auto before = gpu_view_stats();
        const auto fallback = render_view(mesh, W, H, view, nullptr, &slots, &textures);
        set_gpu_view_enabled(false);
        const int calls = backend.last.calls;
        const auto software = render_view(mesh, W, H, view, nullptr, &slots, &textures);
        assert(backend.last.calls == calls);  // switched off: not asked
        set_gpu_view_enabled(true);
        assert(fallback.pixels == software.pixels);
        assert(gpu_view_stats().gpu_failures == before.gpu_failures + 1U);
        backend.accept = true;
    }

    // 5. Wireframe and UV views stay on the CPU.
    {
        const int calls = backend.last.calls;
        ViewState wire = view;
        wire.wireframe = true;
        (void)render_view(mesh, W, H, wire, nullptr, &slots, &textures);
        ViewState uv = view;
        uv.uv_layout = true;
        (void)render_view(mesh, W, H, uv, nullptr, &slots, &textures);
        assert(backend.last.calls == calls);
    }

    // 6. Room: built on demand with prepare_room's rules; same key while the
    //    room is the same.
    {
        Mesh room;
        room.vertices = {{-50.0F, 0.0F, -50.0F}, {50.0F, 0.0F, -50.0F}, {50.0F, 0.0F, 50.0F},
                         {-50.0F, 0.0F, 50.0F}, {0.0F, 30.0F, 0.0F}};
        room.uv0.assign(5U, Vec2{0.25F, 0.25F});
        room.color0.assign(5U, std::array<std::uint8_t, 4>{128U, 128U, 128U, 128U});
        room.blend0 = {0U, 0U, 0U, 0U, 2U};
        room.indices = {0U, 1U, 2U, 0U, 2U, 3U, 4U, 0U, 1U};
        const std::vector<std::uint32_t> room_slots{0U, 0U, 0U};
        const std::vector<std::uint8_t> soft{0U, 1U, 0U};
        ViewState with_room = view;
        with_room.room_mesh = &room;
        with_room.room_texture_slots = &room_slots;
        with_room.room_textures = &textures;
        with_room.room_translucent_triangles = &soft;
        const ViewState::RoomScroll scroll{0U, 0.01F, 0.0F};
        with_room.room_scrolls = {&scroll, 1U};
        with_room.room_time = 30.0F;
        (void)render_view(mesh, W, H, with_room, nullptr, &slots, &textures);
        assert(backend.last.room);
        assert(backend.last.floor.empty());  // the room replaces the floor
        const auto key = backend.last.room_key;
        const auto& g = backend.last.room_geometry;
        assert(g.vertices.size() == 9U);
        std::size_t opaque = 0U, soft_batches = 0U, additive = 0U;
        for (const auto& b : g.batches) {
            assert(b.colored && b.light_base == 1.0F && b.light_gain == 0.0F);
            assert(b.scroll_slot == 0U);
            if (b.kind == GpuBatchKind::room_additive) {
                ++additive;
            } else if (b.translucent) {
                ++soft_batches;
            } else {
                ++opaque;
            }
        }
        assert(opaque == 1U && soft_batches == 1U && additive == 1U);
        assert(g.radius > 50.0F);
        // Far plane reaches the room.
        assert(backend.last.far_z > 60.0F);
        (void)render_view(mesh, W, H, with_room, nullptr, &slots, &textures);
        assert(backend.last.room_key == key);
        room.vertices[4].y = 31.0F;
        (void)render_view(mesh, W, H, with_room, nullptr, &slots, &textures);
        assert(!(backend.last.room_key == key));
    }

    // 7. Graphics settings reach the frame (defaults: 4x MSAA, mipmaps, 8x
    //    anisotropy), clamped to the supported range.
    {
        const GpuViewOptions defaults = gpu_view_options();
        assert(defaults.msaa_samples == 4 && defaults.mipmaps && defaults.anisotropy == 8);
        set_gpu_view_options({0, false, 99});
        (void)render_view(mesh, W, H, view, nullptr, &slots, &textures);
        assert(backend.last.options.msaa_samples == 0);
        assert(!backend.last.options.mipmaps);
        assert(backend.last.options.anisotropy == 16);
        set_gpu_view_options(defaults);
    }

    set_gpu_view_backend(nullptr);
    assert(view_renderer_description() == "CPU (software)");
    return 0;
}
