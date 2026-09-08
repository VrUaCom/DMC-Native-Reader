#include "dmc_rengine/codecs/dds_bc.hpp"
#include "dmcresource/decode_pipeline.h"
#include "dmcresource/resource_capabilities.h"

#include <cassert>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <span>
#include <vector>

namespace {

namespace dds_bc = dmc::rengine::codecs::dds_bc;

void put_u16(std::vector<std::uint8_t>& bytes, std::size_t offset,
             std::uint16_t value) {
    assert(offset + 2U <= bytes.size());
    bytes[offset + 0U] = static_cast<std::uint8_t>(value & 0xFFU);
    bytes[offset + 1U] = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
}

void put_u32(std::vector<std::uint8_t>& bytes, std::size_t offset,
             std::uint32_t value) {
    assert(offset + 4U <= bytes.size());
    for (std::size_t i = 0U; i < 4U; ++i) {
        bytes[offset + i] = static_cast<std::uint8_t>(
            (value >> (i * 8U)) & 0xFFU);
    }
}

std::vector<std::uint8_t> make_dds(bool dxt5 = false,
                                   std::uint32_t mip_count = 3U) {
    const std::size_t block_bytes = dxt5 ? 16U : 8U;
    assert(mip_count >= 1U && mip_count <= 3U);
    std::vector<std::uint8_t> bytes(
        128U + block_bytes * static_cast<std::size_t>(mip_count), 0U);
    bytes[0] = 'D'; bytes[1] = 'D'; bytes[2] = 'S'; bytes[3] = ' ';
    put_u32(bytes, 4U, 124U);
    // DDSD_MIPMAPCOUNT must be present because this fixture explicitly carries
    // mip_count levels. Without it the portable reader correctly treats the
    // image as one mip and parse_exact_dds rejects the remaining payload bytes.
    put_u32(bytes, 8U, 0x000A1007U);
    put_u32(bytes, 12U, 4U);
    put_u32(bytes, 16U, 4U);
    put_u32(bytes, 20U, static_cast<std::uint32_t>(block_bytes));
    put_u32(bytes, 28U, mip_count);
    put_u32(bytes, 76U, 32U);
    put_u32(bytes, 80U, 4U);
    bytes[84] = 'D'; bytes[85] = 'X'; bytes[86] = 'T';
    bytes[87] = dxt5 ? '5' : '1';
    put_u32(bytes, 108U, mip_count > 1U ? 0x00401008U : 0x00001000U);

    if (dxt5) {
        bytes[128U] = 255U;
        bytes[129U] = 0U;
        put_u16(bytes, 136U, 0xF800U);  // red
        put_u16(bytes, 138U, 0x07E0U);  // green
        put_u32(bytes, 140U, 0U);       // all color index 0
    } else {
        put_u16(bytes, 128U, 0xF800U);  // red
        put_u16(bytes, 130U, 0x07E0U);  // green
        put_u32(bytes, 132U, 0U);       // all color index 0
    }
    return bytes;
}

std::vector<std::uint8_t> make_descriptor(
    const std::vector<std::uint8_t>& dds,
    bool dxt5) {
    constexpr std::uint32_t width = 4U;
    constexpr std::uint32_t height = 4U;
    constexpr std::uint32_t mip_count = 3U;
    const auto payload_size = static_cast<std::uint32_t>(dds.size() - 128U);

    std::vector<std::uint8_t> descriptor(0x70U, 0U);
    put_u32(descriptor, 0x08U,
            0x20000U | (mip_count << 8U) | (dxt5 ? 0x88U : 0x86U));
    put_u32(descriptor, 0x0CU, 0xAAE4U);
    put_u32(descriptor, 0x10U, (height << 16U) | width);
    put_u32(descriptor, 0x14U, 1U);
    put_u32(descriptor, 0x18U, width * (dxt5 ? 4U : 2U));
    put_u32(descriptor, 0x20U, 0x40U);
    put_u32(descriptor, 0x38U, payload_size);
    put_u32(descriptor, 0x44U, (height << 16U) | width);
    put_u32(descriptor, 0x48U,
            std::bit_cast<std::uint32_t>(1.0F / static_cast<float>(width)));
    put_u32(descriptor, 0x4CU,
            std::bit_cast<std::uint32_t>(1.0F / static_cast<float>(height)));
    put_u32(descriptor, 0x60U, dxt5 ? 4U : 0U);
    put_u32(descriptor, 0x64U, static_cast<std::uint32_t>(dds.size()));
    put_u32(descriptor, 0x68U, 8U);
    return descriptor;
}

std::vector<std::uint8_t> make_wrapped_dds(bool dxt5 = false) {
    const auto dds = make_dds(dxt5);
    const auto descriptor = make_descriptor(dds, dxt5);
    std::vector<std::uint8_t> bytes(descriptor.size() + dds.size(), 0U);
    std::memcpy(bytes.data(), descriptor.data(), descriptor.size());
    std::memcpy(bytes.data() + descriptor.size(), dds.data(), dds.size());
    return bytes;
}

std::vector<std::uint8_t> make_ptx_zero_final_span(bool dxt5 = false) {
    const auto dds = make_dds(dxt5);
    const auto descriptor = make_descriptor(dds, dxt5);
    std::vector<std::uint8_t> bytes(0x800U + descriptor.size() + dds.size(), 0U);
    put_u32(bytes, 0U, 1U);
    put_u32(bytes, 4U, 0U);
    std::memcpy(bytes.data() + 0x800U, descriptor.data(), descriptor.size());
    std::memcpy(bytes.data() + 0x870U, dds.data(), dds.size());
    return bytes;
}

std::vector<std::uint8_t> make_ptx_sector_bounded() {
    const auto dds = make_dds(false);
    const auto descriptor = make_descriptor(dds, false);
    std::vector<std::uint8_t> bytes(0x1000U, 0U);
    put_u32(bytes, 0U, 1U);
    put_u32(bytes, 4U, 1U);
    std::memcpy(bytes.data() + 0x800U, descriptor.data(), descriptor.size());
    std::memcpy(bytes.data() + 0x870U, dds.data(), dds.size());
    return bytes;
}

std::span<const std::byte> as_bytes(const std::vector<std::uint8_t>& bytes) {
    return std::as_bytes(std::span<const std::uint8_t>{bytes.data(), bytes.size()});
}

void require_red_preview(const dmcresource::ImagePreview& image) {
    assert(image.available());
    assert(image.width == 4U);
    assert(image.height == 4U);
    assert(image.rgba8.size() == 64U);
    for (std::size_t i = 0U; i < 16U; ++i) {
        const auto o = i * 4U;
        assert(image.rgba8[o + 0U] == 255U);
        assert(image.rgba8[o + 1U] == 0U);
        assert(image.rgba8[o + 2U] == 0U);
        assert(image.rgba8[o + 3U] == 255U);
    }
}

}  // namespace

int main() {
    using dmcresource::ResourceCapability;
    using dmcresource::has_capability;
    using dmcresource::run_decode_pipeline;

    for (const bool dxt5 : {false, true}) {
        const auto dds = make_dds(dxt5);
        const auto parsed = dds_bc::parse(as_bytes(dds));
        assert(parsed.ok());
        const auto preview = dds_bc::decode_base_mip_rgba8(
            as_bytes(dds), parsed.document);
        assert(preview.ok);
        require_red_preview({
            preview.image.width,
            preview.image.height,
            preview.image.rgba8});

        const auto result = run_decode_pipeline(
            dxt5 ? "sample_dxt5.dds" : "sample_dxt1.dds",
            dds.data(), dds.size());
        assert(result.accepted);
        assert(!result.renderable);
        assert(has_capability(result.capabilities,
                              ResourceCapability::ImagePreview));
        require_red_preview(result.image_preview);

        const auto capped = dds_bc::decode_base_mip_rgba8(
            as_bytes(dds), parsed.document, 15U);
        assert(!capped.ok);
        assert(!capped.image.available());
    }

    const auto partial_mips = make_dds(false, 1U);
    const auto partial_parse = dds_bc::parse(as_bytes(partial_mips));
    assert(partial_parse.ok());
    const auto partial_result = run_decode_pipeline(
        "standalone_partial.dds", partial_mips.data(), partial_mips.size());
    assert(partial_result.accepted);
    require_red_preview(partial_result.image_preview);

    const auto wrapped = make_wrapped_dds(false);
    const auto wrapped_result = run_decode_pipeline(
        "wrapped.dds", wrapped.data(), wrapped.size());
    assert(wrapped_result.accepted);
    require_red_preview(wrapped_result.image_preview);
    assert(wrapped_result.inspection.root.children.size() == 1U);

    std::vector<std::uint8_t> huge(128U, 0U);
    huge[0] = 'D'; huge[1] = 'D'; huge[2] = 'S'; huge[3] = ' ';
    put_u32(huge, 4U, 124U);
    put_u32(huge, 12U, std::numeric_limits<std::uint32_t>::max());
    put_u32(huge, 16U, std::numeric_limits<std::uint32_t>::max());
    put_u32(huge, 28U, 32U);
    put_u32(huge, 76U, 32U);
    put_u32(huge, 80U, 4U);
    huge[84] = 'D'; huge[85] = 'X'; huge[86] = 'T'; huge[87] = '5';
    const auto huge_result = dds_bc::parse(as_bytes(huge));
    assert(!huge_result.ok());

    const auto ptx = make_ptx_zero_final_span();
    const auto ptx_ok = run_decode_pipeline("sample.ptx", ptx.data(), ptx.size());
    assert(ptx_ok.accepted);
    assert(has_capability(ptx_ok.capabilities,
                          ResourceCapability::ChildResources));
    assert(!has_capability(ptx_ok.capabilities,
                           ResourceCapability::ImagePreview));
    assert(ptx_ok.inspection.root.children.size() == 1U);
    assert(ptx_ok.inspection.root.children[0].children.size() == 1U);

    assert(ptx_ok.children.size() == 1U);
    const auto& child = ptx_ok.children[0];
    assert(child.title == "DDS 0");
    assert(child.suggested_filename == "texture_0.dds");
    assert(child.probe.format == dmcresource::Format::Dds);
    assert(child.probe.content_confirmed);
    assert(has_capability(child.capabilities, ResourceCapability::Inspection));
    assert(has_capability(child.capabilities, ResourceCapability::ImagePreview));
    assert(child.inspection.format == "DDS");
    assert(child.inspection.root.title == "Texture 0");
    require_red_preview(child.image_preview);

    auto bad_descriptor_size = ptx;
    put_u32(bad_descriptor_size, 0x800U + 0x64U, 1U);
    assert(!run_decode_pipeline("bad.ptx", bad_descriptor_size.data(),
                                bad_descriptor_size.size()).accepted);

    auto trailing = ptx;
    trailing.push_back(0U);
    assert(!run_decode_pipeline("trailing.ptx", trailing.data(),
                                trailing.size()).accepted);

    auto sector_oob = make_ptx_sector_bounded();
    put_u32(sector_oob, 4U, 2U);
    assert(!run_decode_pipeline("sector_oob.ptx", sector_oob.data(),
                                sector_oob.size()).accepted);

    auto nonzero_padding = make_ptx_sector_bounded();
    nonzero_padding.back() = 1U;
    assert(!run_decode_pipeline("padding.ptx", nonzero_padding.data(),
                                nonzero_padding.size()).accepted);

    return 0;
}
