#include "dmcresource/decode_pipeline.h"
#include "dmcresource/dmc_resource.h"
#include "dmcresource/resource_capabilities.h"

#include <bit>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace {

void put_u32(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint32_t value) {
    assert(offset + 4U <= bytes.size());
    for (std::size_t index = 0U; index < 4U; ++index) {
        bytes[offset + index] = static_cast<std::uint8_t>(
            (value >> (index * 8U)) & 0xFFU);
    }
}

[[nodiscard]] std::uint32_t dxt5_payload(std::uint32_t width, std::uint32_t height) {
    return ((width + 3U) / 4U) * ((height + 3U) / 4U) * 16U;
}

[[nodiscard]] std::vector<std::uint8_t> single_level_dds(
    std::uint32_t width,
    std::uint32_t height) {
    const auto payload = dxt5_payload(width, height);
    std::vector<std::uint8_t> out(0x80U + payload, 0U);
    out[0] = 'D'; out[1] = 'D'; out[2] = 'S'; out[3] = ' ';
    put_u32(out, 0x04U, 124U);
    put_u32(out, 0x08U, 0x00081007U);
    put_u32(out, 0x0CU, height);
    put_u32(out, 0x10U, width);
    put_u32(out, 0x14U, payload);
    put_u32(out, 0x1CU, 0U);
    put_u32(out, 0x4CU, 32U);
    put_u32(out, 0x50U, 4U);
    out[0x54U] = 'D'; out[0x55U] = 'X'; out[0x56U] = 'T'; out[0x57U] = '5';
    put_u32(out, 0x6CU, 0x00001000U);
    return out;
}

void fill_common_descriptor(
    std::vector<std::uint8_t>& descriptor,
    std::uint32_t dimensions,
    std::uint32_t reciprocal_width,
    std::uint32_t reciprocal_height,
    std::uint32_t dds_size) {
    put_u32(descriptor, 0x0CU, 0xAAE4U);
    put_u32(descriptor, 0x10U, dimensions);
    put_u32(descriptor, 0x14U, 1U);
    put_u32(descriptor, 0x20U, 0x40U);
    put_u32(descriptor, 0x44U, dimensions);
    put_u32(descriptor, 0x48U, reciprocal_width);
    put_u32(descriptor, 0x4CU, reciprocal_height);
    put_u32(descriptor, 0x64U, dds_size);
    put_u32(descriptor, 0x68U, 8U);
}

[[nodiscard]] std::vector<std::uint8_t> legacy_ptx() {
    constexpr std::uint32_t width = 4U;
    constexpr std::uint32_t height = 4U;
    const auto dds = single_level_dds(width, height);
    std::vector<std::uint8_t> descriptor(0x70U, 0U);
    fill_common_descriptor(
        descriptor,
        (height << 16U) | width,
        std::bit_cast<std::uint32_t>(1.0F / 4.0F),
        std::bit_cast<std::uint32_t>(1.0F / 4.0F),
        static_cast<std::uint32_t>(dds.size()));
    put_u32(descriptor, 0x08U, 0x00020185U);
    put_u32(descriptor, 0x18U, 0U);
    put_u32(descriptor, 0x38U, 0U);
    put_u32(descriptor, 0x60U, 4U);

    std::vector<std::uint8_t> out(0x1000U, 0U);
    put_u32(out, 0U, 1U);
    put_u32(out, 4U, 1U);
    std::memcpy(out.data() + 0x800U, descriptor.data(), descriptor.size());
    std::memcpy(out.data() + 0x870U, dds.data(), dds.size());
    return out;
}

[[nodiscard]] std::vector<std::uint8_t> legacy_tm2_named_wrapped_dds() {
    constexpr std::uint32_t descriptor_width = 4U;
    constexpr std::uint32_t descriptor_height = 4U;
    constexpr std::uint32_t dds_width = 8U;
    constexpr std::uint32_t dds_height = 8U;
    const auto dds = single_level_dds(dds_width, dds_height);
    std::vector<std::uint8_t> descriptor(0x70U, 0U);
    fill_common_descriptor(
        descriptor,
        (descriptor_height << 16U) | descriptor_width,
        std::bit_cast<std::uint32_t>(1.0F / 4.0F),
        std::bit_cast<std::uint32_t>(1.0F / 4.0F),
        static_cast<std::uint32_t>(dds.size()));
    put_u32(descriptor, 0x08U, 0x000201A5U);
    put_u32(descriptor, 0x18U, descriptor_width * 4U);
    put_u32(descriptor, 0x38U, dxt5_payload(dds_width, dds_height));
    put_u32(descriptor, 0x60U, 5U);

    std::vector<std::uint8_t> out(descriptor.size() + dds.size(), 0U);
    std::memcpy(out.data(), descriptor.data(), descriptor.size());
    std::memcpy(out.data() + descriptor.size(), dds.data(), dds.size());
    return out;
}

[[nodiscard]] std::vector<std::uint8_t> evt_table() {
    std::vector<std::uint8_t> out(0x40U, 0U);
    out[0] = 'E'; out[1] = 'V'; out[2] = 'T'; out[3] = 0U;
    put_u32(out, 0x04U, 0x00010001U);
    put_u32(out, 0x20U, 0x00000102U);
    put_u32(out, 0x24U, 0x37U);
    put_u32(out, 0x28U, 0x00000001U);
    put_u32(out, 0x2CU, 0x00000020U);
    put_u32(out, 0x08U, 0x2CU);
    return out;
}

} // namespace

int main() {
    using namespace dmcresource;

    const auto ptx = legacy_ptx();
    const auto ptx_result = run_decode_pipeline(
        "basic.ptx", ptx.data(), ptx.size());
    assert(ptx_result.accepted);
    assert(ptx_result.probe.format == Format::Ptx);
    assert(ptx_result.children.size() == 1U);
    assert(ptx_result.children[0].image_preview.available());

    const auto tm2 = legacy_tm2_named_wrapped_dds();
    const auto tm2_probe = probe("i001_90.tm2", tm2.data(), tm2.size());
    assert(tm2_probe.recognized);
    assert(tm2_probe.format == Format::Dds);
    const auto tm2_result = run_decode_pipeline(
        "i001_90.tm2", tm2.data(), tm2.size());
    assert(tm2_result.accepted);
    assert(tm2_result.image_preview.available());
    assert(has_capability(tm2_result.capabilities, ResourceCapability::ImagePreview));
    assert(tm2_result.inspection.root.title == "Wrapped DDS");

    const auto event = evt_table();
    const auto evt_probe = probe("EventTbl20.bin", event.data(), event.size());
    assert(evt_probe.recognized);
    assert(evt_probe.content_confirmed);
    assert(evt_probe.format == Format::Evt);
    const auto evt_result = run_decode_pipeline(
        "EventTbl20.bin", event.data(), event.size());
    assert(evt_result.accepted);
    assert(!evt_result.renderable);
    assert(evt_result.inspection.format == "EVT");
    assert(has_capability(evt_result.capabilities, ResourceCapability::Inspection));
    assert(!evt_result.inspection.root.children.empty());
    const auto& commands = evt_result.inspection.root.children.back();
    assert(commands.title == "Commands");
    assert(commands.children.size() == 3U);
    assert(commands.children.front().properties[1].key == "Opcode");
    assert(commands.children.front().properties[1].value == "0x02");

    auto malformed = event;
    put_u32(malformed, 0x20U, 0x00010102U);
    assert(!run_decode_pipeline(
        "EventTbl20.bin", malformed.data(), malformed.size()).accepted);

    return 0;
}
