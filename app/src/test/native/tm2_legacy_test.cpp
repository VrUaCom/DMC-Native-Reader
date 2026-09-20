#include "dmcresource/decode_pipeline.h"
#include "dmcresource/resource_capabilities.h"

#include <cassert>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

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
            (value >> (8U * i)) & 0xFFU);
    }
}

std::vector<std::uint8_t> make_legacy_tm2_wrapped_dxt5() {
    // Corpus shape recovered from i001_90.tm2:
    // 0x70 DMC descriptor + one standard DDS DXT5 base level.
    constexpr std::uint32_t descriptor_width = 2U;
    constexpr std::uint32_t descriptor_height = 2U;
    constexpr std::uint32_t dds_width = 4U;
    constexpr std::uint32_t dds_height = 4U;
    constexpr std::uint32_t payload_size = 16U;
    constexpr std::uint32_t dds_size = 128U + payload_size;

    std::vector<std::uint8_t> bytes(0x70U + dds_size, 0U);

    put_u32(bytes, 0x08U, 0x000201A5U);
    put_u32(bytes, 0x0CU, 0x0000AAE4U);
    put_u32(bytes, 0x10U, (descriptor_height << 16U) | descriptor_width);
    put_u32(bytes, 0x14U, 1U);
    put_u32(bytes, 0x18U, descriptor_width * 4U);
    put_u32(bytes, 0x20U, 0x40U);
    put_u32(bytes, 0x38U, payload_size);
    put_u32(bytes, 0x44U, (descriptor_height << 16U) | descriptor_width);
    put_u32(bytes, 0x48U,
            std::bit_cast<std::uint32_t>(1.0F / descriptor_width));
    put_u32(bytes, 0x4CU,
            std::bit_cast<std::uint32_t>(1.0F / descriptor_height));
    put_u32(bytes, 0x60U, 5U);
    put_u32(bytes, 0x64U, dds_size);
    put_u32(bytes, 0x68U, 8U);

    constexpr std::size_t dds = 0x70U;
    bytes[dds + 0U] = 'D';
    bytes[dds + 1U] = 'D';
    bytes[dds + 2U] = 'S';
    bytes[dds + 3U] = ' ';
    put_u32(bytes, dds + 4U, 124U);
    put_u32(bytes, dds + 8U, 0x00081007U);
    put_u32(bytes, dds + 12U, dds_height);
    put_u32(bytes, dds + 16U, dds_width);
    put_u32(bytes, dds + 20U, payload_size);
    put_u32(bytes, dds + 28U, 0U);  // raw DDS mip count: one base level
    put_u32(bytes, dds + 76U, 32U);
    put_u32(bytes, dds + 80U, 4U);
    bytes[dds + 84U] = 'D';
    bytes[dds + 85U] = 'X';
    bytes[dds + 86U] = 'T';
    bytes[dds + 87U] = '5';
    put_u32(bytes, dds + 108U, 0x00001000U);

    // One opaque-red BC3/DXT5 block.
    constexpr std::size_t block = dds + 128U;
    bytes[block + 0U] = 255U;
    bytes[block + 1U] = 0U;
    put_u16(bytes, block + 8U, 0xF800U);
    put_u16(bytes, block + 10U, 0x07E0U);
    put_u32(bytes, block + 12U, 0U);
    return bytes;
}

}  // namespace

int main() {
    using dmcresource::ResourceCapability;
    using dmcresource::has_capability;

    const auto tm2 = make_legacy_tm2_wrapped_dxt5();
    const auto result = dmcresource::run_decode_pipeline(
        "i001_90.tm2", tm2.data(), tm2.size());

    assert(result.accepted);
    assert(result.probe.format == dmcresource::Format::Dds);
    assert(!result.probe.content_confirmed);
    assert(has_capability(result.capabilities, ResourceCapability::Inspection));
    assert(has_capability(result.capabilities, ResourceCapability::ImagePreview));
    assert(result.image_preview.available());
    assert(result.image_preview.width == 4U);
    assert(result.image_preview.height == 4U);
    assert(result.image_preview.rgba8.size() == 4U * 4U * 4U);

    for (std::size_t pixel = 0U; pixel < 16U; ++pixel) {
        const auto offset = pixel * 4U;
        assert(result.image_preview.rgba8[offset + 0U] == 255U);
        assert(result.image_preview.rgba8[offset + 1U] == 0U);
        assert(result.image_preview.rgba8[offset + 2U] == 0U);
        assert(result.image_preview.rgba8[offset + 3U] == 255U);
    }

    assert(result.inspection.root.children.size() == 1U);
    return 0;
}
