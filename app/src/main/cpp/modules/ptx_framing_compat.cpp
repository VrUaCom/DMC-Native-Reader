#include "dmcresource/ptx_framing_compat.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <limits>

namespace dmcresource::ptx_compat {

namespace dmc3 = dmc::rengine::profiles::dmc3;

namespace {

constexpr std::size_t kSector = 0x800U;
constexpr std::size_t kDescriptor = 0x70U;
constexpr std::size_t kDdsHeader = 128U;
constexpr std::uint32_t kMaxTextures = 4096U;

[[nodiscard]] std::uint32_t u32(std::span<const std::byte> s, std::size_t o) noexcept {
    std::uint32_t v = 0U;
    for (std::size_t i = 0U; i < 4U; ++i) {
        v |= std::to_integer<std::uint32_t>(s[o + i]) << (8U * i);
    }
    return v;
}

[[nodiscard]] std::uint64_t dxt_chain_size(std::uint32_t w, std::uint32_t h,
                                           std::uint32_t mips, std::uint32_t block) noexcept {
    std::uint64_t total = 0U;
    for (std::uint32_t level = 0U; level < std::max(mips, 1U); ++level) {
        const std::uint64_t bw = std::max<std::uint64_t>(1U, (std::max(w >> level, 1U) + 3U) / 4U);
        const std::uint64_t bh = std::max<std::uint64_t>(1U, (std::max(h >> level, 1U) + 3U) / 4U);
        total += bw * bh * block;
    }
    return total;
}

// Tolerant read for bundles written by community tools. Uses only what a
// reader needs to reach the pixels: the bundle header (count + per-slot sector
// spans), the fixed 0x70 descriptor size, the DDS header and payload. The
// descriptor's other fields (format word, secondary dimensions, reciprocal
// floats, auxiliary pair) are ignored because such tools leave them zeroed
// or copied from another slot. The canonical strict reader stays the
// authority; this path is only tried after it rejects the bytes.
[[nodiscard]] dmc3::TextureSlotFramingResult parse_lenient(std::span<const std::byte> s) {
    dmc3::TextureSlotFramingResult out;
    out.status = dmc3::TextureSlotFramingStatus::not_recognized;
    if (s.size() < kSector * 2U) return out;
    const auto count = u32(s, 0U);
    if (count == 0U || count > kMaxTextures || 4U + std::size_t{count} * 4U > kSector) return out;

    std::uint64_t sector = 1U;
    out.document.kind = dmc3::TextureSlotFramingKind::texture_bundle;
    out.document.slot_size = s.size();
    for (std::uint32_t index = 0U; index < count; ++index) {
        const auto span = u32(s, 4U + std::size_t{index} * 4U);
        const std::uint64_t desc = sector * kSector;
        const std::uint64_t dds = desc + kDescriptor;
        if (span == 0U || desc + std::uint64_t{span} * kSector > s.size() ||
            dds + kDdsHeader > s.size()) {
            return {};
        }
        const auto d = static_cast<std::size_t>(dds);
        if (std::to_integer<char>(s[d]) != 'D' || std::to_integer<char>(s[d + 1U]) != 'D' ||
            std::to_integer<char>(s[d + 2U]) != 'S' || std::to_integer<char>(s[d + 3U]) != ' ') {
            return {};
        }
        const auto height = u32(s, d + 12U);
        const auto width = u32(s, d + 16U);
        const auto mips = std::max(u32(s, d + 28U), 1U);
        const auto fourcc = u32(s, d + 84U);
        dmc3::TextureCompressionKind kind{};
        std::uint32_t block = 0U;
        if (fourcc == 0x31545844U) { kind = dmc3::TextureCompressionKind::dxt1; block = 8U; }
        else if (fourcc == 0x35545844U) { kind = dmc3::TextureCompressionKind::dxt5; block = 16U; }
        else return {};
        if (width == 0U || height == 0U || width > 8192U || height > 8192U || mips > 16U) return {};

        const auto payload = dxt_chain_size(width, height, mips, block);
        const std::uint64_t room = desc + std::uint64_t{span} * kSector - dds;
        std::uint64_t size = u32(s, static_cast<std::size_t>(desc) + 0x64U);
        if (size < kDdsHeader + payload || size > room) size = kDdsHeader + payload;
        if (size > room) return {};

        // Structure is still strict: sector padding after the DDS is zero.
        for (std::uint64_t i = dds + size; i < desc + std::uint64_t{span} * kSector; ++i) {
            if (s[static_cast<std::size_t>(i)] != std::byte{0}) return {};
        }

        dmc3::TextureSlotEntry entry{};
        entry.texture_index = index;
        entry.descriptor_offset = desc;
        entry.dds_offset = dds;
        entry.dds_size = static_cast<std::uint32_t>(size);
        entry.dds_payload_size = static_cast<std::uint32_t>(size - kDdsHeader);
        entry.width = width;
        entry.height = height;
        entry.mip_map_count = mips;
        entry.compression = kind;
        entry.secondary_width = width;
        entry.secondary_height = height;
        entry.sector_span = span;
        out.document.textures.push_back(entry);
        sector += span;
    }
    // No trailing bytes past the last declared sector span.
    if (sector * kSector != s.size()) return {};
    out.status = dmc3::TextureSlotFramingStatus::ok;
    out.detail = "community-tool descriptors (lenient read: header, sector spans, DDS only)";
    return out;
}

[[nodiscard]] std::uint32_t u16(std::span<const std::byte> s, std::size_t o) noexcept {
    return std::to_integer<std::uint32_t>(s[o]) | std::to_integer<std::uint32_t>(s[o + 1U]) << 8U;
}

// Retail single-level PTX (interface packs id*.pac, checked on id900.pac).
// Same framing as every PTX (dmc3.exe 0x140336BB0): count + sector spans in
// the first 0x800 bytes, then per texture at its sector a 0x70 serialized
// gfxTexture object (vtable slot zero on disk, fixed up in place by
// 0x140046510) followed by a standard DDS loaded with DirectXTK. Here the
// DDS holds the base level only (mip count 0, DDSD_MIPMAPCOUNT clear).
// gfxTexture fields checked against the DDS:
//   +0x10 u16 logical width, +0x12 u16 logical height,
//   +0x18 u32 row bytes of 4x4 blocks,
//   +0x44 u16 x2 secondary size (= logical size),
//   +0x64 u32 DDS size = 128 + base level (image record +0x04).
// Accepted only when all of these agree and the sector padding after each
// DDS is zero. Rengine reads the same bundles as
// TextureSlotReadVariant::legacy_single_mip_interface_bundle_dxt5.
[[nodiscard]] dmc3::TextureSlotFramingResult parse_single_level(std::span<const std::byte> s) {
    dmc3::TextureSlotFramingResult out;
    out.status = dmc3::TextureSlotFramingStatus::not_recognized;
    if (s.size() < kSector * 2U || s.size() % kSector != 0U) return out;
    const auto count = u32(s, 0U);
    if (count == 0U || count > kMaxTextures || 4U + std::size_t{count} * 4U > kSector) return out;
    for (std::size_t i = 4U + std::size_t{count} * 4U; i < kSector; ++i) {
        if (s[i] != std::byte{0}) return out;
    }
    std::uint64_t sector = 1U;
    out.document.kind = dmc3::TextureSlotFramingKind::texture_bundle;
    out.document.slot_size = s.size();
    for (std::uint32_t index = 0U; index < count; ++index) {
        const auto span = u32(s, 4U + std::size_t{index} * 4U);
        const std::uint64_t desc = sector * kSector;
        const std::uint64_t dds = desc + kDescriptor;
        if (span == 0U || desc + std::uint64_t{span} * kSector > s.size()) return {};
        const auto d = static_cast<std::size_t>(dds);
        const auto o = static_cast<std::size_t>(desc);
        if (std::to_integer<char>(s[d]) != 'D' || std::to_integer<char>(s[d + 1U]) != 'D' ||
            std::to_integer<char>(s[d + 2U]) != 'S' || std::to_integer<char>(s[d + 3U]) != ' ' ||
            u32(s, d + 4U) != 124U) {
            return {};
        }
        constexpr std::uint32_t kDdsdMipmapCount = 0x20000U;
        const auto flags = u32(s, d + 8U);
        const auto height = u32(s, d + 12U);
        const auto width = u32(s, d + 16U);
        const auto mips = u32(s, d + 28U);
        const auto fourcc = u32(s, d + 84U);
        if (mips > 1U || (flags & kDdsdMipmapCount) != 0U) return {};
        dmc3::TextureCompressionKind kind{};
        std::uint32_t block = 0U;
        if (fourcc == 0x31545844U) { kind = dmc3::TextureCompressionKind::dxt1; block = 8U; }
        else if (fourcc == 0x35545844U) { kind = dmc3::TextureCompressionKind::dxt5; block = 16U; }
        else return {};
        if (width == 0U || height == 0U || width > 8192U || height > 8192U) return {};
        const auto payload = dxt_chain_size(width, height, 1U, block);
        const std::uint64_t size = kDdsHeader + payload;
        const std::uint64_t end = desc + std::uint64_t{span} * kSector;
        if (dds + size > end) return {};
        const auto row_bytes = std::max<std::uint32_t>(1U, (width + 3U) / 4U) * block;
        if (u16(s, o + 0x10U) != width || u16(s, o + 0x12U) != height ||
            u32(s, o + 0x18U) != row_bytes || u32(s, o + 0x64U) != size ||
            u16(s, o + 0x44U) != width || u16(s, o + 0x46U) != height) {
            return {};
        }
        for (std::uint64_t i = dds + size; i < end; ++i) {
            if (s[static_cast<std::size_t>(i)] != std::byte{0}) return {};
        }
        dmc3::TextureSlotEntry entry{};
        entry.texture_index = index;
        entry.descriptor_offset = desc;
        entry.dds_offset = dds;
        entry.dds_size = static_cast<std::uint32_t>(size);
        entry.dds_payload_size = static_cast<std::uint32_t>(payload);
        entry.width = width;
        entry.height = height;
        entry.mip_map_count = 1U;
        entry.compression = kind;
        entry.secondary_width = width;
        entry.secondary_height = height;
        entry.sector_span = span;
        out.document.textures.push_back(entry);
        sector += span;
    }
    if (sector * kSector != s.size()) return {};
    out.status = dmc3::TextureSlotFramingStatus::ok;
    out.detail = "single-level PTX (base-level DDS without mip chain)";
    return out;
}

}  // namespace

dmc3::TextureSlotFramingResult parse_texture_bundle(
    std::span<const std::byte> source,
    bool* compatibility_used,
    bool* community_descriptors_used,
    bool* single_level_used) {
    if (community_descriptors_used != nullptr) *community_descriptors_used = false;
    if (single_level_used != nullptr) *single_level_used = false;
    const auto read = dmc3::TextureSlotFramingReader::parse(source);

    // Retain the existing product diagnostic bit for the historical DXT1
    // auxiliary-mode case even though the pinned ReaderCore now owns that rule.
    // The newly promoted single-level DXT5 variants are already represented by
    // ReaderCore evidence and do not masquerade as the old aux workaround.
    if (compatibility_used != nullptr) {
        *compatibility_used = read.ok() && std::any_of(
            read.framing.document.textures.begin(),
            read.framing.document.textures.end(),
            [](const dmc3::TextureSlotEntry& entry) {
                return entry.compression == dmc3::TextureCompressionKind::dxt1 &&
                    entry.auxiliary_mode != 0U;
            });
    }
    if (read.framing.ok()) return read.framing;
    // The canonical reader accepts only full mip chains; retail single-level
    // bundles fail its DDS domain check and get the strict single-level read.
    if (read.framing.status == dmc3::TextureSlotFramingStatus::invalid_dds) {
        try {
            auto single = parse_single_level(source);
            if (single.ok()) {
                if (compatibility_used != nullptr) *compatibility_used = false;
                if (single_level_used != nullptr) *single_level_used = true;
                return single;
            }
        } catch (...) {
        }
    }
    // Only descriptor-field disagreements are tolerated. Structural faults
    // (padding, trailing bytes, sector bounds, DDS validity) stay rejected.
    if (read.framing.status != dmc3::TextureSlotFramingStatus::descriptor_mismatch) {
        return read.framing;
    }

    try {
        auto lenient = parse_lenient(source);
        if (lenient.ok()) {
            if (compatibility_used != nullptr) *compatibility_used = false;
            if (community_descriptors_used != nullptr) *community_descriptors_used = true;
            return lenient;
        }
    } catch (...) {
    }
    return read.framing;
}

}  // namespace dmcresource::ptx_compat
