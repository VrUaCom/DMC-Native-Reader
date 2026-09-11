#include "dmcresource/ptx_framing_compat.h"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <vector>

namespace dmcresource::ptx_compat {
namespace {

constexpr std::size_t kSectorSize = 0x800U;
constexpr std::size_t kDescriptorSize = 0x70U;
constexpr std::size_t kDdsHeaderSize = 0x80U;

constexpr std::size_t kDescriptorEncodingOffset = 0x08U;
constexpr std::size_t kDescriptorConstant0cOffset = 0x0CU;
constexpr std::size_t kDescriptorDimensionsOffset = 0x10U;
constexpr std::size_t kDescriptorConstant14Offset = 0x14U;
constexpr std::size_t kDescriptorRowBytesOffset = 0x18U;
constexpr std::size_t kDescriptorConstant20Offset = 0x20U;
constexpr std::size_t kDescriptorPayloadSizeOffset = 0x38U;
constexpr std::size_t kAuxModeOffset = 0x3CU;
constexpr std::size_t kAuxValueOffset = 0x40U;
constexpr std::size_t kDescriptorSecondaryDimensionsOffset = 0x44U;
constexpr std::size_t kDescriptorReciprocalWidthOffset = 0x48U;
constexpr std::size_t kDescriptorReciprocalHeightOffset = 0x4CU;
constexpr std::size_t kDescriptorFormatOffset = 0x60U;
constexpr std::size_t kDescriptorDdsSizeOffset = 0x64U;
constexpr std::size_t kDescriptorConstant68Offset = 0x68U;

constexpr std::size_t kDdsFlagsOffset = 8U;
constexpr std::size_t kDdsHeightOffset = 12U;
constexpr std::size_t kDdsWidthOffset = 16U;
constexpr std::size_t kDdsLinearSizeOffset = 20U;
constexpr std::size_t kDdsDepthOffset = 24U;
constexpr std::size_t kDdsMipCountOffset = 28U;
constexpr std::size_t kDdsPixelFormatSizeOffset = 76U;
constexpr std::size_t kDdsPixelFormatFlagsOffset = 80U;
constexpr std::size_t kDdsFourCcOffset = 84U;
constexpr std::size_t kDdsCapsOffset = 108U;
constexpr std::size_t kDdsCaps2Offset = 112U;

constexpr std::uint32_t kSingleMipDxt5Encoding = 0x00020185U;
constexpr std::uint32_t kSingleMipDdsFlags = 0x00081007U;
constexpr std::uint32_t kSingleMipDdsCaps = 0x00001000U;

constexpr std::array<std::size_t, 13> kDescriptorZeroOffsets{
    0x00U, 0x04U, 0x1CU, 0x24U, 0x28U, 0x2CU, 0x30U,
    0x34U, 0x50U, 0x54U, 0x58U, 0x5CU, 0x6CU,
};

[[nodiscard]] bool read_u32_le(
    std::span<const std::byte> bytes,
    std::size_t offset,
    std::uint32_t* out) noexcept {
    if (out == nullptr || offset > bytes.size() || bytes.size() - offset < 4U) {
        return false;
    }
    *out = std::to_integer<std::uint32_t>(bytes[offset + 0U]) |
        (std::to_integer<std::uint32_t>(bytes[offset + 1U]) << 8U) |
        (std::to_integer<std::uint32_t>(bytes[offset + 2U]) << 16U) |
        (std::to_integer<std::uint32_t>(bytes[offset + 3U]) << 24U);
    return true;
}

void write_u32_le(
    std::vector<std::byte>* bytes,
    std::size_t offset,
    std::uint32_t value) noexcept {
    if (bytes == nullptr || offset > bytes->size() || bytes->size() - offset < 4U) {
        return;
    }
    (*bytes)[offset + 0U] = static_cast<std::byte>(value & 0xFFU);
    (*bytes)[offset + 1U] = static_cast<std::byte>((value >> 8U) & 0xFFU);
    (*bytes)[offset + 2U] = static_cast<std::byte>((value >> 16U) & 0xFFU);
    (*bytes)[offset + 3U] = static_cast<std::byte>((value >> 24U) & 0xFFU);
}

[[nodiscard]] bool all_zero(
    std::span<const std::byte> bytes,
    std::size_t begin,
    std::size_t end) noexcept {
    if (begin > end || end > bytes.size()) return false;
    for (std::size_t i = begin; i < end; ++i) {
        if (bytes[i] != std::byte{0}) return false;
    }
    return true;
}

[[nodiscard]] bool descriptor_zero_fields_are_zero(
    std::span<const std::byte> bytes,
    std::size_t descriptor_offset) noexcept {
    for (const auto relative : kDescriptorZeroOffsets) {
        std::uint32_t value = 0U;
        if (!read_u32_le(bytes, descriptor_offset + relative, &value) || value != 0U) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool dxt1_at_descriptor(
    std::span<const std::byte> bytes,
    std::size_t descriptor_offset) noexcept {
    if (descriptor_offset > std::numeric_limits<std::size_t>::max() -
            kDescriptorSize - kDdsFourCcOffset) {
        return false;
    }
    const auto fourcc = descriptor_offset + kDescriptorSize + kDdsFourCcOffset;
    return fourcc <= bytes.size() && bytes.size() - fourcc >= 4U &&
        bytes[fourcc + 0U] == std::byte{'D'} &&
        bytes[fourcc + 1U] == std::byte{'X'} &&
        bytes[fourcc + 2U] == std::byte{'T'} &&
        bytes[fourcc + 3U] == std::byte{'1'};
}

[[nodiscard]] std::optional<dmc3::TextureSlotFramingResult>
parse_single_mip_dxt5_bundle(std::span<const std::byte> source) {
    // Two user-supplied retail PTX samples (basic.ptx and at.ptx) expose a
    // second DMC3 texture descriptor ABI: one texture, one base DXT5 mip,
    // sector-bounded padding, and no DDS mip-map flag/count. ReaderCore's
    // canonical parser currently requires a complete mip pyramid, so it rejects
    // this otherwise bounded DDS before Native Reader can expose the child.
    // Keep this compatibility route intentionally narrow until the canonical
    // format contract is promoted upstream.
    if (source.size() < kSectorSize + kDescriptorSize + kDdsHeaderSize) {
        return std::nullopt;
    }

    std::uint32_t texture_count = 0U;
    std::uint32_t sector_span = 0U;
    if (!read_u32_le(source, 0U, &texture_count) || texture_count != 1U ||
        !read_u32_le(source, 4U, &sector_span) || sector_span == 0U ||
        sector_span > std::numeric_limits<std::size_t>::max() / kSectorSize) {
        return std::nullopt;
    }
    if (!all_zero(source, 8U, kSectorSize)) return std::nullopt;

    const auto bounded_end = kSectorSize +
        static_cast<std::size_t>(sector_span) * kSectorSize;
    if (bounded_end != source.size()) return std::nullopt;

    const std::size_t descriptor_offset = kSectorSize;
    const std::size_t dds_offset = descriptor_offset + kDescriptorSize;
    if (dds_offset > source.size() || source.size() - dds_offset < kDdsHeaderSize) {
        return std::nullopt;
    }
    if (source[dds_offset + 0U] != std::byte{'D'} ||
        source[dds_offset + 1U] != std::byte{'D'} ||
        source[dds_offset + 2U] != std::byte{'S'} ||
        source[dds_offset + 3U] != std::byte{' '}) {
        return std::nullopt;
    }

    std::uint32_t header_size = 0U;
    std::uint32_t dds_flags = 0U;
    std::uint32_t width = 0U;
    std::uint32_t height = 0U;
    std::uint32_t linear_size = 0U;
    std::uint32_t depth = 0U;
    std::uint32_t raw_mip_count = 0U;
    std::uint32_t pixel_format_size = 0U;
    std::uint32_t pixel_format_flags = 0U;
    std::uint32_t caps = 0U;
    std::uint32_t caps2 = 0U;
    if (!read_u32_le(source, dds_offset + 4U, &header_size) ||
        !read_u32_le(source, dds_offset + kDdsFlagsOffset, &dds_flags) ||
        !read_u32_le(source, dds_offset + kDdsWidthOffset, &width) ||
        !read_u32_le(source, dds_offset + kDdsHeightOffset, &height) ||
        !read_u32_le(source, dds_offset + kDdsLinearSizeOffset, &linear_size) ||
        !read_u32_le(source, dds_offset + kDdsDepthOffset, &depth) ||
        !read_u32_le(source, dds_offset + kDdsMipCountOffset, &raw_mip_count) ||
        !read_u32_le(source, dds_offset + kDdsPixelFormatSizeOffset, &pixel_format_size) ||
        !read_u32_le(source, dds_offset + kDdsPixelFormatFlagsOffset, &pixel_format_flags) ||
        !read_u32_le(source, dds_offset + kDdsCapsOffset, &caps) ||
        !read_u32_le(source, dds_offset + kDdsCaps2Offset, &caps2)) {
        return std::nullopt;
    }
    if (header_size != 124U || dds_flags != kSingleMipDdsFlags ||
        width == 0U || height == 0U || depth != 0U || raw_mip_count != 0U ||
        pixel_format_size != 32U || pixel_format_flags != 4U ||
        caps != kSingleMipDdsCaps || caps2 != 0U ||
        source[dds_offset + kDdsFourCcOffset + 0U] != std::byte{'D'} ||
        source[dds_offset + kDdsFourCcOffset + 1U] != std::byte{'X'} ||
        source[dds_offset + kDdsFourCcOffset + 2U] != std::byte{'T'} ||
        source[dds_offset + kDdsFourCcOffset + 3U] != std::byte{'5'}) {
        return std::nullopt;
    }

    const auto blocks_w = (static_cast<std::uint64_t>(width) + 3ULL) / 4ULL;
    const auto blocks_h = (static_cast<std::uint64_t>(height) + 3ULL) / 4ULL;
    if (blocks_w != 0U &&
        blocks_h > std::numeric_limits<std::uint64_t>::max() / blocks_w) {
        return std::nullopt;
    }
    const auto blocks = blocks_w * blocks_h;
    if (blocks > std::numeric_limits<std::uint32_t>::max() / 16ULL) {
        return std::nullopt;
    }
    const auto payload_size = static_cast<std::uint32_t>(blocks * 16ULL);
    if (linear_size != payload_size) return std::nullopt;

    std::uint32_t descriptor_encoding = 0U;
    std::uint32_t descriptor_dimensions = 0U;
    std::uint32_t descriptor_row_bytes = 0U;
    std::uint32_t descriptor_payload_size = 0U;
    std::uint32_t auxiliary_mode = 0U;
    std::uint32_t auxiliary_value = 0U;
    std::uint32_t secondary_dimensions = 0U;
    std::uint32_t reciprocal_width = 0U;
    std::uint32_t reciprocal_height = 0U;
    std::uint32_t descriptor_format = 0U;
    std::uint32_t dds_size = 0U;
    std::uint32_t constant0c = 0U;
    std::uint32_t constant14 = 0U;
    std::uint32_t constant20 = 0U;
    std::uint32_t constant68 = 0U;
    if (!read_u32_le(source, descriptor_offset + kDescriptorEncodingOffset, &descriptor_encoding) ||
        !read_u32_le(source, descriptor_offset + kDescriptorDimensionsOffset, &descriptor_dimensions) ||
        !read_u32_le(source, descriptor_offset + kDescriptorRowBytesOffset, &descriptor_row_bytes) ||
        !read_u32_le(source, descriptor_offset + kDescriptorPayloadSizeOffset, &descriptor_payload_size) ||
        !read_u32_le(source, descriptor_offset + kAuxModeOffset, &auxiliary_mode) ||
        !read_u32_le(source, descriptor_offset + kAuxValueOffset, &auxiliary_value) ||
        !read_u32_le(source, descriptor_offset + kDescriptorSecondaryDimensionsOffset, &secondary_dimensions) ||
        !read_u32_le(source, descriptor_offset + kDescriptorReciprocalWidthOffset, &reciprocal_width) ||
        !read_u32_le(source, descriptor_offset + kDescriptorReciprocalHeightOffset, &reciprocal_height) ||
        !read_u32_le(source, descriptor_offset + kDescriptorFormatOffset, &descriptor_format) ||
        !read_u32_le(source, descriptor_offset + kDescriptorDdsSizeOffset, &dds_size) ||
        !read_u32_le(source, descriptor_offset + kDescriptorConstant0cOffset, &constant0c) ||
        !read_u32_le(source, descriptor_offset + kDescriptorConstant14Offset, &constant14) ||
        !read_u32_le(source, descriptor_offset + kDescriptorConstant20Offset, &constant20) ||
        !read_u32_le(source, descriptor_offset + kDescriptorConstant68Offset, &constant68)) {
        return std::nullopt;
    }

    const auto packed_dimensions = (height << 16U) | width;
    const auto secondary_width = secondary_dimensions & 0xFFFFU;
    const auto secondary_height = secondary_dimensions >> 16U;
    if (width > 0xFFFFU || height > 0xFFFFU ||
        descriptor_encoding != kSingleMipDxt5Encoding ||
        descriptor_dimensions != packed_dimensions ||
        descriptor_row_bytes != 0U || descriptor_payload_size != 0U ||
        auxiliary_mode != 0U || auxiliary_value != 0U ||
        secondary_width != width || secondary_height != height ||
        reciprocal_width != std::bit_cast<std::uint32_t>(
            1.0F / static_cast<float>(width)) ||
        reciprocal_height != std::bit_cast<std::uint32_t>(
            1.0F / static_cast<float>(height)) ||
        descriptor_format != 4U || constant0c != 0xAAE4U ||
        constant14 != 1U || constant20 != 0x40U || constant68 != 8U ||
        !descriptor_zero_fields_are_zero(source, descriptor_offset)) {
        return std::nullopt;
    }

    const auto expected_dds_size = static_cast<std::uint64_t>(kDdsHeaderSize) + payload_size;
    if (expected_dds_size > std::numeric_limits<std::uint32_t>::max() ||
        dds_size != expected_dds_size ||
        static_cast<std::uint64_t>(dds_offset) + dds_size > bounded_end) {
        return std::nullopt;
    }
    const auto dds_end = dds_offset + static_cast<std::size_t>(dds_size);
    if (!all_zero(source, dds_end, bounded_end)) return std::nullopt;

    dmc3::TextureSlotFramingDocument document{
        .kind = dmc3::TextureSlotFramingKind::texture_bundle,
        .slot_size = source.size(),
        .textures = {
            dmc3::TextureSlotEntry{
                .texture_index = 0U,
                .descriptor_offset = descriptor_offset,
                .dds_offset = dds_offset,
                .dds_size = dds_size,
                .dds_payload_size = payload_size,
                .width = width,
                .height = height,
                .mip_map_count = 1U,
                .compression = dmc3::TextureCompressionKind::dxt5,
                .secondary_width = secondary_width,
                .secondary_height = secondary_height,
                .auxiliary_mode = 0U,
                .auxiliary_value = 0U,
                .sector_span = sector_span,
            },
        },
    };
    if (!document.valid()) return std::nullopt;

    return dmc3::TextureSlotFramingResult{
        .status = dmc3::TextureSlotFramingStatus::ok,
        .document = std::move(document),
        .detail = "Native Reader compatibility: corpus-confirmed single-base-mip DXT5 PTX",
    };
}

}  // namespace

dmc3::TextureSlotFramingResult parse_texture_bundle(
    std::span<const std::byte> source,
    bool* compatibility_used) {
    if (compatibility_used != nullptr) *compatibility_used = false;

    auto direct = dmc3::TextureSlotFramingParser::parse(source);
    if (direct.ok()) return direct;

    if (auto single_mip = parse_single_mip_dxt5_bundle(source);
        single_mip.has_value()) {
        return std::move(*single_mip);
    }

    if (direct.detail !=
        "descriptor auxiliary pair lies outside the corpus-confirmed bounded relation") {
        return direct;
    }

    std::uint32_t texture_count = 0U;
    if (!read_u32_le(source, 0U, &texture_count) || texture_count == 0U) {
        return direct;
    }
    if (texture_count > (kSectorSize - 4U) / 4U) {
        return direct;
    }
    const auto header_bytes = 4U + static_cast<std::size_t>(texture_count) * 4U;
    if (header_bytes > source.size() || header_bytes > kSectorSize) {
        return direct;
    }

    std::vector<std::byte> patched(source.begin(), source.end());
    std::size_t descriptor_offset = kSectorSize;
    bool changed = false;

    for (std::uint32_t index = 0U; index < texture_count; ++index) {
        if (descriptor_offset > source.size() ||
            source.size() - descriptor_offset < kDescriptorSize) {
            return direct;
        }

        std::uint32_t auxiliary_mode = 0U;
        std::uint32_t auxiliary_value = 0U;
        if (!read_u32_le(
                source, descriptor_offset + kAuxModeOffset, &auxiliary_mode) ||
            !read_u32_le(
                source, descriptor_offset + kAuxValueOffset, &auxiliary_value)) {
            return direct;
        }

        if (dxt1_at_descriptor(source, descriptor_offset) &&
            (auxiliary_mode == 1U || auxiliary_mode == 2U) &&
            auxiliary_value != 0U) {
            write_u32_le(&patched, descriptor_offset + kAuxModeOffset, 0U);
            write_u32_le(&patched, descriptor_offset + kAuxValueOffset, 0U);
            changed = true;
        }

        if (index + 1U < texture_count) {
            std::uint32_t next_sector_span = 0U;
            if (!read_u32_le(
                    source, 4U + static_cast<std::size_t>(index) * 4U,
                    &next_sector_span) ||
                next_sector_span == 0U ||
                next_sector_span > std::numeric_limits<std::size_t>::max() / kSectorSize) {
                return direct;
            }
            const auto advance = static_cast<std::size_t>(next_sector_span) * kSectorSize;
            if (descriptor_offset > std::numeric_limits<std::size_t>::max() - advance) {
                return direct;
            }
            descriptor_offset += advance;
        }
    }

    if (!changed) return direct;

    auto retried = dmc3::TextureSlotFramingParser::parse(
        std::span<const std::byte>{patched.data(), patched.size()});
    if (!retried.ok() ||
        retried.document.kind != dmc3::TextureSlotFramingKind::texture_bundle) {
        return direct;
    }

    if (compatibility_used != nullptr) *compatibility_used = true;
    return retried;
}

}  // namespace dmcresource::ptx_compat
