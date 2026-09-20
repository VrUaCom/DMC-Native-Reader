#include <algorithm>
#include <bit>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

#include "dmcresource/decode_pipeline.h"
#include "dmcresource/model_texture_binding.h"
#include "dmcresource/texture_set.h"
#include "dmcresource/view_renderer.h"

namespace {

void put_u16(std::vector<std::uint8_t>& bytes,
             std::size_t offset,
             std::uint16_t value) {
    assert(offset + 2U <= bytes.size());
    bytes[offset + 0U] = static_cast<std::uint8_t>(value & 0xFFU);
    bytes[offset + 1U] = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
}

void put_u32(std::vector<std::uint8_t>& bytes,
             std::size_t offset,
             std::uint32_t value) {
    assert(offset + 4U <= bytes.size());
    for (std::size_t i = 0U; i < 4U; ++i) {
        bytes[offset + i] = static_cast<std::uint8_t>(
            (value >> (i * 8U)) & 0xFFU);
    }
}

std::span<const std::byte> as_bytes(const std::vector<std::uint8_t>& bytes) {
    return std::as_bytes(
        std::span<const std::uint8_t>{bytes.data(), bytes.size()});
}

std::vector<std::uint8_t> make_single_mip_dxt5_ptx() {
    constexpr std::uint32_t width = 4U;
    constexpr std::uint32_t height = 4U;
    constexpr std::uint32_t payload_size = 16U;
    constexpr std::uint32_t dds_size = 128U + payload_size;
    constexpr std::size_t descriptor_offset = 0x800U;
    constexpr std::size_t dds_offset = 0x870U;

    std::vector<std::uint8_t> bytes(0x1000U, 0U);
    put_u32(bytes, 0U, 1U);
    put_u32(bytes, 4U, 1U);

    put_u32(bytes, descriptor_offset + 0x08U, 0x00020185U);
    put_u32(bytes, descriptor_offset + 0x0CU, 0xAAE4U);
    put_u32(bytes, descriptor_offset + 0x10U, (height << 16U) | width);
    put_u32(bytes, descriptor_offset + 0x14U, 1U);
    put_u32(bytes, descriptor_offset + 0x18U, 0U);
    put_u32(bytes, descriptor_offset + 0x20U, 0x40U);
    put_u32(bytes, descriptor_offset + 0x38U, 0U);
    put_u32(bytes, descriptor_offset + 0x3CU, 0U);
    put_u32(bytes, descriptor_offset + 0x40U, 0U);
    put_u32(bytes, descriptor_offset + 0x44U, (height << 16U) | width);
    put_u32(bytes, descriptor_offset + 0x48U,
            std::bit_cast<std::uint32_t>(1.0F / static_cast<float>(width)));
    put_u32(bytes, descriptor_offset + 0x4CU,
            std::bit_cast<std::uint32_t>(1.0F / static_cast<float>(height)));
    put_u32(bytes, descriptor_offset + 0x60U, 4U);
    put_u32(bytes, descriptor_offset + 0x64U, dds_size);
    put_u32(bytes, descriptor_offset + 0x68U, 8U);

    bytes[dds_offset + 0U] = 'D';
    bytes[dds_offset + 1U] = 'D';
    bytes[dds_offset + 2U] = 'S';
    bytes[dds_offset + 3U] = ' ';
    put_u32(bytes, dds_offset + 4U, 124U);
    put_u32(bytes, dds_offset + 8U, 0x00081007U);
    put_u32(bytes, dds_offset + 12U, height);
    put_u32(bytes, dds_offset + 16U, width);
    put_u32(bytes, dds_offset + 20U, payload_size);
    put_u32(bytes, dds_offset + 24U, 0U);
    put_u32(bytes, dds_offset + 28U, 0U);
    put_u32(bytes, dds_offset + 76U, 32U);
    put_u32(bytes, dds_offset + 80U, 4U);
    bytes[dds_offset + 84U] = 'D';
    bytes[dds_offset + 85U] = 'X';
    bytes[dds_offset + 86U] = 'T';
    bytes[dds_offset + 87U] = '5';
    put_u32(bytes, dds_offset + 108U, 0x00001000U);
    put_u32(bytes, dds_offset + 112U, 0U);

    const auto block = dds_offset + 128U;
    bytes[block + 0U] = 255U;
    bytes[block + 1U] = 0U;
    put_u16(bytes, block + 8U, 0xF800U);
    put_u16(bytes, block + 10U, 0x07E0U);
    put_u32(bytes, block + 12U, 0U);
    return bytes;
}

void require_single_mip_ptx_compatibility() {
    const auto ptx = make_single_mip_dxt5_ptx();
    const auto set = dmcresource::texture_set::parse_ptx(as_bytes(ptx));
    assert(set.ok());
    assert(set.kind == dmcresource::texture_set::Kind::ptx_bundle);
    assert(set.slots.size() == 1U);
    assert(set.slots[0].index == 0U);
    assert(set.slots[0].dds_offset == 0x870U);
    assert(set.slots[0].dds_size == 144U);
    assert(set.slots[0].dds.width == 4U);
    assert(set.slots[0].dds.height == 4U);
    assert(set.slots[0].dds.mip_count == 1U);

    dmcresource::ImagePreview preview;
    std::string detail;
    assert(dmcresource::texture_set::decode_base_mip(
        as_bytes(ptx), set.slots[0], &preview, &detail));
    assert(preview.available());
    assert(preview.width == 4U);
    assert(preview.height == 4U);
    for (std::size_t offset = 0U; offset < preview.rgba8.size(); offset += 4U) {
        assert(preview.rgba8[offset + 0U] == 255U);
        assert(preview.rgba8[offset + 1U] == 0U);
        assert(preview.rgba8[offset + 2U] == 0U);
        assert(preview.rgba8[offset + 3U] == 255U);
    }

    const auto pipeline = dmcresource::run_decode_pipeline(
        "single-mip.ptx", ptx.data(), ptx.size());
    assert(pipeline.accepted);
    assert(pipeline.children.size() == 1U);
    assert(pipeline.children[0].probe.format == dmcresource::Format::Dds);
    assert(pipeline.children[0].image_preview.available());
}

}  // namespace

int main() {
    using namespace dmcresource;

    require_single_mip_ptx_compatibility();

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

    // Composite parts retain RenderScene authority and deliberately do not keep
    // a second per-part flattened Mesh. The scene-native validation route must
    // therefore accept exactly the same canonical triangle-slot projection.
    model_texture_binding::RequiredSlots scene_required;
    assert(model_texture_binding::collect_required_slots(
        scene, slots, &scene_required));
    assert(scene_required.slots.size() == 1U);
    assert(scene_required.slots[0] == 1U);
    assert(scene_required.max_slot == 1U);
    assert(model_texture_binding::can_attach_texture_companion(scene, slots));

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
