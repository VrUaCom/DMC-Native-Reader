#include "dmcresource/ptx_framing_compat.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace dmcresource::ptx_compat {
namespace {

constexpr std::size_t kSectorSize = 0x800U;
constexpr std::size_t kDescriptorSize = 0x70U;
constexpr std::size_t kAuxModeOffset = 0x3CU;
constexpr std::size_t kAuxValueOffset = 0x40U;
constexpr std::size_t kDdsFourCcOffset = 84U;

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

}  // namespace

dmc3::TextureSlotFramingResult parse_texture_bundle(
    std::span<const std::byte> source,
    bool* compatibility_used) {
    if (compatibility_used != nullptr) *compatibility_used = false;

    auto direct = dmc3::TextureSlotFramingParser::parse(source);
    if (direct.ok() ||
        direct.detail !=
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
            std::uint32_t sector_span = 0U;
            if (!read_u32_le(
                    source, 4U + static_cast<std::size_t>(index) * 4U,
                    &sector_span) ||
                sector_span == 0U ||
                sector_span > std::numeric_limits<std::size_t>::max() / kSectorSize) {
                return direct;
            }
            const auto advance = static_cast<std::size_t>(sector_span) * kSectorSize;
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
