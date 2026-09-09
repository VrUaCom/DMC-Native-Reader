#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <vector>

#include "dmc_rengine/profiles/dmc3/texture_slot_framing.hpp"

namespace dmcresource::ptx_compat {

namespace dmc3 = dmc::rengine::profiles::dmc3;

namespace detail {

constexpr std::size_t kSectorSize = 0x800U;
constexpr std::size_t kDescriptorSize = 0x70U;
constexpr std::size_t kAuxModeOffset = 0x3CU;
constexpr std::size_t kAuxValueOffset = 0x40U;
constexpr std::size_t kDdsFourCcOffset = 84U;

[[nodiscard]] inline bool read_u32_le(
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

inline void write_u32_le(
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

[[nodiscard]] inline bool dxt1_at_descriptor(
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

}  // namespace detail

// Native Reader is temporarily pinned to a ReaderCore revision whose canonical
// texture-slot parser still carries one obsolete corpus rule: non-zero
// auxiliary mode was coupled to DXT5. Retail em000 disproves that coupling
// (DXT1 + mode 2 + non-zero value).
//
// This is deliberately not a second PTX parser. We first run the canonical
// parser unchanged. Only when it fails on that exact legacy auxiliary-pair
// diagnostic do we make a temporary copy, neutralize only the affected
// DXT1 auxiliary pairs, and run the same canonical parser again. All physical
// framing, sector bounds, DDS sizes, mip chains and descriptor fields remain
// canonical-parser authority. On success the original auxiliary values are
// restored into the typed result.
[[nodiscard]] inline dmc3::TextureSlotFramingResult parse_texture_bundle(
    std::span<const std::byte> source,
    bool* compatibility_used = nullptr) {
    if (compatibility_used != nullptr) *compatibility_used = false;

    auto direct = dmc3::TextureSlotFramingParser::parse(source);
    if (direct.ok() ||
        direct.detail !=
            "descriptor auxiliary pair lies outside the corpus-confirmed bounded relation") {
        return direct;
    }

    std::uint32_t texture_count = 0U;
    if (!detail::read_u32_le(source, 0U, &texture_count) || texture_count == 0U) {
        return direct;
    }
    if (texture_count > (detail::kSectorSize - 4U) / 4U) {
        return direct;
    }
    const auto header_bytes = 4U + static_cast<std::size_t>(texture_count) * 4U;
    if (header_bytes > source.size() || header_bytes > detail::kSectorSize) {
        return direct;
    }

    std::vector<std::byte> patched(source.begin(), source.end());
    std::size_t descriptor_offset = detail::kSectorSize;
    bool changed = false;

    for (std::uint32_t index = 0U; index < texture_count; ++index) {
        if (descriptor_offset > source.size() ||
            source.size() - descriptor_offset < detail::kDescriptorSize) {
            return direct;
        }

        std::uint32_t auxiliary_mode = 0U;
        std::uint32_t auxiliary_value = 0U;
        if (!detail::read_u32_le(
                source, descriptor_offset + detail::kAuxModeOffset,
                &auxiliary_mode) ||
            !detail::read_u32_le(
                source, descriptor_offset + detail::kAuxValueOffset,
                &auxiliary_value)) {
            return direct;
        }

        if (detail::dxt1_at_descriptor(source, descriptor_offset) &&
            (auxiliary_mode == 1U || auxiliary_mode == 2U) &&
            auxiliary_value != 0U) {
            detail::write_u32_le(
                &patched, descriptor_offset + detail::kAuxModeOffset, 0U);
            detail::write_u32_le(
                &patched, descriptor_offset + detail::kAuxValueOffset, 0U);
            changed = true;
        }

        if (index + 1U < texture_count) {
            std::uint32_t sector_span = 0U;
            if (!detail::read_u32_le(
                    source, 4U + static_cast<std::size_t>(index) * 4U,
                    &sector_span) ||
                sector_span == 0U ||
                sector_span > std::numeric_limits<std::size_t>::max() /
                    detail::kSectorSize) {
                return direct;
            }
            const auto advance =
                static_cast<std::size_t>(sector_span) * detail::kSectorSize;
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

    for (auto& entry : retried.document.textures) {
        const auto descriptor = static_cast<std::size_t>(entry.descriptor_offset);
        std::uint32_t auxiliary_mode = 0U;
        std::uint32_t auxiliary_value = 0U;
        if (!detail::read_u32_le(
                source, descriptor + detail::kAuxModeOffset, &auxiliary_mode) ||
            !detail::read_u32_le(
                source, descriptor + detail::kAuxValueOffset, &auxiliary_value)) {
            return direct;
        }
        entry.auxiliary_mode = auxiliary_mode;
        entry.auxiliary_value = auxiliary_value;
    }

    if (compatibility_used != nullptr) *compatibility_used = true;
    return retried;
}

}  // namespace dmcresource::ptx_compat
