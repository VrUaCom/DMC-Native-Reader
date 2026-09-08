#include "dmcresource/decode_pipeline.h"
#include "dmcresource/formats/dds.h"
#include "dmcresource/resource_capabilities.h"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <span>
#include <vector>

namespace {

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

std::vector<std::uint8_t> make_dds(bool dxt5 = false) {
    const std::size_t block_bytes = dxt5 ? 16U : 8U;
    std::vector<std::uint8_t> bytes(128U + block_bytes * 3U, 0U);
    bytes[0] = 'D'; bytes[1] = 'D'; bytes[2] = 'S'; bytes[3] = ' ';
    put_u32(bytes, 4U, 124U);
    put_u32(bytes, 12U, 4U);
    put_u32(bytes, 16U, 4U);
    put_u32(bytes, 28U, 3U);
    put_u32(bytes, 76U, 32U);
    bytes[84] = 'D'; bytes[85] = 'X'; bytes[86] = 'T';
    bytes[87] = dxt5 ? '5' : '1';

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

std::vector<std::uint8_t> make_descriptor_wrapped_dds(bool dxt5 = false) {
    const auto dds = make_dds(dxt5);
    std::vector<std::uint8_t> bytes(0x70U + dds.size(), 0U);
    put_u32(bytes, 0x38U, static_cast<std::uint32_t>(dds.size() - 128U));
    put_u32(bytes, 0x64U, static_cast<std::uint32_t>(dds.size()));
    std::memcpy(bytes.data() + 0x70U, dds.data(), dds.size());
    return bytes;
}

std::vector<std::uint8_t> make_ptx_zero_final_span() {
    const auto dds = make_dds(false);
    std::vector<std::uint8_t> bytes(0x800U + 0x70U + dds.size(), 0U);
    put_u32(bytes, 0U, 1U);
    put_u32(bytes, 4U, 0U);
    put_u32(bytes, 0x800U + 0x38U,
            static_cast<std::uint32_t>(dds.size() - 128U));
    put_u32(bytes, 0x800U + 0x64U,
            static_cast<std::uint32_t>(dds.size()));
    std::memcpy(bytes.data() + 0x870U, dds.data(), dds.size());
    return bytes;
}

std::vector<std::uint8_t> make_ptx_sector_bounded() {
    const auto dds = make_dds(false);
    std::vector<std::uint8_t> bytes(0x1000U, 0U);
    put_u32(bytes, 0U, 1U);
    put_u32(bytes, 4U, 1U);
    put_u32(bytes, 0x800U + 0x38U,
            static_cast<std::uint32_t>(dds.size() - 128U));
    put_u32(bytes, 0x800U + 0x64U,
            static_cast<std::uint32_t>(dds.size()));
    std::memcpy(bytes.data() + 0x870U, dds.data(), dds.size());
    return bytes;
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
        const auto parsed = dmcresource::formats::dds::parse(
            std::span<const std::uint8_t>{dds.data(), dds.size()});
        assert(parsed.ok);
        const auto preview = dmcresource::formats::dds::decode_preview(
            std::span<const std::uint8_t>{dds.data(), dds.size()},
            parsed.document);
        assert(preview.ok);
        require_red_preview(preview.image);

        const auto result = run_decode_pipeline(
            dxt5 ? "sample_dxt5.dds" : "sample_dxt1.dds",
            dds.data(), dds.size());
        assert(result.accepted);
        assert(!result.renderable);
        assert(result.probe.content_confirmed);
        assert(has_capability(result.capabilities,
                              ResourceCapability::ImagePreview));
        require_red_preview(result.image_preview);

        const auto capped = dmcresource::formats::dds::decode_preview(
            std::span<const std::uint8_t>{dds.data(), dds.size()},
            parsed.document, 15U);
        assert(!capped.ok);
        assert(!capped.image.available());
    }

    // DMC resources are observed with a bounded partial mip chain. A base-only
    // DDS is valid when its declared payload is present; zero bytes after the
    // intrinsic DDS are accepted as carrier/alignment padding.
    auto base_only = make_dds(false);
    put_u32(base_only, 28U, 1U);
    const auto base_only_parsed = dmcresource::formats::dds::parse(
        std::span<const std::uint8_t>{base_only.data(), base_only.size()});
    assert(base_only_parsed.ok);
    assert(base_only_parsed.document.mip_count == 1U);
    assert(base_only_parsed.document.total_size == 136U);

    const auto base_only_result = run_decode_pipeline(
        "base_only.dds", base_only.data(), base_only.size());
    assert(base_only_result.accepted);
    assert(base_only_result.probe.content_confirmed);
    require_red_preview(base_only_result.image_preview);

    auto nonzero_trailing = base_only;
    nonzero_trailing.back() = 1U;
    assert(!run_decode_pipeline("nonzero_tail.dds", nonzero_trailing.data(),
                                nonzero_trailing.size()).accepted);

    // Some extracted DMC texture resources retain the canonical 0x70-byte
    // texture descriptor in front of the DDS. The wrapper is accepted only
    // when +0x38 payload bytes and +0x64 total DDS bytes match exactly.
    const auto wrapped = make_descriptor_wrapped_dds(true);
    const auto wrapped_ok = run_decode_pipeline(
        "descriptor_wrapped.dds", wrapped.data(), wrapped.size());
    assert(wrapped_ok.accepted);
    assert(wrapped_ok.probe.content_confirmed);
    assert(wrapped_ok.inspection.root.source_span.has_value());
    assert(wrapped_ok.inspection.root.source_span->offset == 0x70U);
    require_red_preview(wrapped_ok.image_preview);

    auto bad_wrapped = wrapped;
    put_u32(bad_wrapped, 0x64U, 1U);
    assert(!run_decode_pipeline("bad_descriptor.dds", bad_wrapped.data(),
                                bad_wrapped.size()).accepted);

    // More mip levels than can exist for the declared dimensions remain invalid.
    auto too_many_mips = make_dds(false);
    put_u32(too_many_mips, 28U, 4U);
    assert(!dmcresource::formats::dds::parse(
        std::span<const std::uint8_t>{too_many_mips.data(),
                                     too_many_mips.size()}).ok);

    // Huge dimensions must fail arithmetic before any large allocation/read.
    std::vector<std::uint8_t> huge(128U, 0U);
    huge[0] = 'D'; huge[1] = 'D'; huge[2] = 'S'; huge[3] = ' ';
    put_u32(huge, 4U, 124U);
    put_u32(huge, 12U, std::numeric_limits<std::uint32_t>::max());
    put_u32(huge, 16U, std::numeric_limits<std::uint32_t>::max());
    put_u32(huge, 28U, 32U);
    put_u32(huge, 76U, 32U);
    huge[84] = 'D'; huge[85] = 'X'; huge[86] = 'T'; huge[87] = '5';
    const auto huge_result = dmcresource::formats::dds::parse(
        std::span<const std::uint8_t>{huge.data(), huge.size()});
    assert(!huge_result.ok);
    assert(huge_result.diagnostic.find("overflow") != std::string::npos);

    const auto ptx = make_ptx_zero_final_span();
    const auto ptx_ok = run_decode_pipeline("sample.ptx", ptx.data(), ptx.size());
    assert(ptx_ok.accepted);
    assert(has_capability(ptx_ok.capabilities,
                          ResourceCapability::ChildResources));
    assert(!has_capability(ptx_ok.capabilities,
                           ResourceCapability::ImagePreview));
    assert(ptx_ok.inspection.root.children.size() == 1U);
    assert(ptx_ok.inspection.root.children[0].children.size() == 1U);

    // PTX must publish the DDS through the generic child-resource projection,
    // including an actual image preview when the bounded gallery budget allows.
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
