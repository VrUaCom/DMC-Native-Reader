#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

#include "dmcresource/view_renderer.h"

int main() {
    using namespace dmcresource;

    RenderScene scene;
    MeshPrimitive primitive;
    primitive.name = "textured-triangle";
    primitive.object_index = 0U;
    primitive.mesh_index = 0U;
    primitive.mesh.vertices = {
        {-1.0F, -1.0F, 0.0F},
        { 1.0F, -1.0F, 0.0F},
        { 0.0F,  1.0F, 0.0F},
    };
    primitive.mesh.indices = {0U, 1U, 2U};
    primitive.mesh.uv0 = {
        {0.0F, 0.0F},
        {1.0F, 0.0F},
        {0.0F, 1.0F},
    };
    scene.meshes.push_back(std::move(primitive));
    scene.textures.push_back(TextureBinding{
        .mesh_primitive = 0U,
        .texture_slot = 1U,
        .external_source = "test companion slot",
    });

    Mesh materialized;
    assert(materialize_render_scene(scene, &materialized));
    assert(materialized.has_uv0());

    std::vector<std::uint32_t> slots;
    assert(materialize_triangle_texture_slots(scene, &slots));
    assert(slots.size() == 1U);
    assert(slots[0] == 1U);

    std::vector<ImagePreview> textures(2U);
    textures[1].width = 2U;
    textures[1].height = 2U;
    textures[1].rgba8 = {
        255U,   0U,   0U, 255U,
          0U, 255U,   0U, 255U,
          0U,   0U, 255U, 255U,
        255U, 255U,   0U, 255U,
    };
    assert(textures[1].available());

    ViewState view;
    view.yaw_radians = 0.0F;
    view.pitch_radians = 0.0F;
    view.zoom = 1.0F;

    const auto image = render_view(
        materialized, 128, 128, view, nullptr, &slots, &textures);
    assert(image.width == 128);
    assert(image.height == 128);
    assert(image.pixels.size() == 128U * 128U * 4U);

    bool saw_texture_colour = false;
    for (std::size_t offset = 0U; offset + 3U < image.pixels.size(); offset += 4U) {
        const auto r = image.pixels[offset + 0U];
        const auto g = image.pixels[offset + 1U];
        const auto b = image.pixels[offset + 2U];
        if ((r == 255U && g == 0U && b == 0U) ||
            (r == 0U && g == 255U && b == 0U) ||
            (r == 0U && g == 0U && b == 255U) ||
            (r == 255U && g == 255U && b == 0U)) {
            saw_texture_colour = true;
            break;
        }
    }
    assert(saw_texture_colour);

    // Without a companion texture set the same geometry still renders through
    // the neutral shaded path instead of becoming invisible.
    const auto fallback = render_view(
        materialized, 128, 128, view, nullptr, nullptr, nullptr);
    assert(fallback.pixels.size() == image.pixels.size());
    assert(fallback.pixels != image.pixels);

    return 0;
}
