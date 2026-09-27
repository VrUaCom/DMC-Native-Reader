#include <cassert>
#include <cstdint>
#include <vector>

#include "dmcresource/resource_capabilities.h"
#include "dmcresource/resource_session.h"
#include "dmcresource/spider/black_widow.h"

namespace {

void put_u16(std::vector<std::uint8_t>& bytes, std::size_t offset,
             std::uint16_t value) {
    bytes[offset + 0U] = static_cast<std::uint8_t>(value & 0xFFU);
    bytes[offset + 1U] = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
}

void put_u32(std::vector<std::uint8_t>& bytes, std::size_t offset,
             std::uint32_t value) {
    for (std::size_t i = 0U; i < 4U; ++i) {
        bytes[offset + i] = static_cast<std::uint8_t>(
            (value >> (i * 8U)) & 0xFFU);
    }
}

std::vector<std::uint8_t> make_red_dxt1_dds() {
    std::vector<std::uint8_t> bytes(136U, 0U);
    bytes[0] = 'D'; bytes[1] = 'D'; bytes[2] = 'S'; bytes[3] = ' ';
    put_u32(bytes, 4U, 124U);
    put_u32(bytes, 8U, 0x000A1007U);
    put_u32(bytes, 12U, 4U);
    put_u32(bytes, 16U, 4U);
    put_u32(bytes, 20U, 8U);
    put_u32(bytes, 28U, 1U);
    put_u32(bytes, 76U, 32U);
    put_u32(bytes, 80U, 4U);
    bytes[84] = 'D'; bytes[85] = 'X'; bytes[86] = 'T'; bytes[87] = '1';
    put_u32(bytes, 108U, 0x00001000U);
    put_u16(bytes, 128U, 0xF800U);
    put_u16(bytes, 130U, 0x0000U);
    put_u32(bytes, 132U, 0U);
    return bytes;
}

}  // namespace

int main() {
    using namespace dmcresource;
    namespace widow = dmcresource::spider::black_widow;

    ChildResource lazy;
    lazy.id = "dds-7";
    lazy.title = "DDS 7";
    lazy.suggested_filename = "texture_7.dds";
    lazy.probe = {Format::Dds, true, true, "DDS", "texture", "recognized",
                  "DATA_CONFIRMED", "image/vnd-ms.dds"};
    lazy.capabilities = capability(ResourceCapability::Inspection) |
                        ResourceCapability::ImagePreview;
    lazy.source_bytes = make_red_dxt1_dds();

    Session gallery;
    gallery.probe = {Format::Ptx, true, true, "PTX", "texture", "recognized",
                     "DATA_CONFIRMED", "application/vnd.dmc.ptx"};
    gallery.capabilities = capability(ResourceCapability::Inspection) |
                           ResourceCapability::ChildResources;
    gallery.children.push_back(lazy);

    const auto gallery_state = black_widow_state(&gallery);
    assert(widow::has_state(gallery_state, widow::StateFlag::ChildBrowserMode));
    assert(widow::has_state(gallery_state, widow::StateFlag::CanExportPng));
    assert(session_child_count(&gallery) == 1U);

    // Gallery tiles now reuse the ordinary child reader when a parent did not
    // keep a resident preview. Android only needs one bounded 256x256 bitmap.
    assert(!gallery.children[0].image_preview.available());
    const auto [thumbnail_width, thumbnail_height] =
        session_child_preview_size(&gallery, 0);
    assert(thumbnail_width == 256U);
    assert(thumbnail_height == 256U);

    ImagePreview thumbnail;
    const auto* thumbnail_view = session_child_preview(&gallery, 0, &thumbnail);
    assert(thumbnail_view == &thumbnail);
    assert(thumbnail.available());
    assert(thumbnail.width == 256U);
    assert(thumbnail.height == 256U);
    // 4x4 DXT1 source fills the square, so the centre must still be red.
    const std::size_t centre =
        (static_cast<std::size_t>(128U) * thumbnail.width + 128U) * 4U;
    assert(thumbnail.rgba8[centre + 0U] == 255U);
    assert(thumbnail.rgba8[centre + 1U] == 0U);
    assert(thumbnail.rgba8[centre + 2U] == 0U);
    assert(thumbnail.rgba8[centre + 3U] == 255U);

    // Selecting the tile still opens the original full-resolution child; the
    // thumbnail path does not replace or mutate the source view.
    auto child = open_session_child(&gallery, 0);
    assert(child != nullptr);
    assert(child->probe.format == Format::Dds);
    assert(child->image_preview.available());
    assert(child->image_preview.width == 4U);
    assert(child->image_preview.height == 4U);
    assert(child->image_preview.rgba8.size() == 64U);
    assert(child->image_preview.rgba8[0] == 255U);
    assert(child->image_preview.rgba8[1] == 0U);
    assert(child->image_preview.rgba8[2] == 0U);
    assert(child->image_preview.rgba8[3] == 255U);

    const auto child_state = black_widow_state(child.get());
    assert(widow::has_state(child_state, widow::StateFlag::CanPreviewImage));
    assert(widow::has_state(child_state, widow::StateFlag::CanExportPng));

    return 0;
}
