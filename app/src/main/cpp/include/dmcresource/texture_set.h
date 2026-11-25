#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "dmcresource/dds_bcn.h"
#include "dmcresource/image_preview.h"

namespace dmcresource::texture_set {

enum class Kind : std::uint8_t {
    invalid = 0,
    standalone_dds,
    wrapped_dds,
    ptx_bundle,
};

struct Slot final {
    std::uint32_t index{};
    std::uint64_t descriptor_offset{};
    std::uint64_t dds_offset{};
    std::uint64_t dds_size{};
    std::uint32_t sector_span{};
    std::uint32_t secondary_width{};
    std::uint32_t secondary_height{};
    // BC1..BC7, legacy FourCC or DX10 header.
    dds_bcn::Document dds;
};

struct ParseResult final {
    Kind kind{Kind::invalid};
    std::vector<Slot> slots;
    bool ptx_aux_compat_used{};
    // Bundle accepted through the lenient community-tool descriptor path.
    bool ptx_community_descriptors{};
    // Retail single-level bundle (DDS base level only, e.g. id*.pac).
    bool ptx_single_level{};
    // Bundle whose DDS children use formats beyond retail DXT1/DXT5 (BC2,
    // BC4..BC7 or a DX10 header), read with the dmc3.exe load-path checks.
    bool ptx_extended_formats{};
    std::string detail;

    [[nodiscard]] bool ok() const noexcept {
        return kind != Kind::invalid && !slots.empty();
    }
};

// Parse exactly one standalone/descriptor-wrapped DDS source into a neutral
// one-slot texture set. Result/diagnostic construction may allocate; callers
// that expose noexcept ABI/action boundaries must catch and fail closed.
[[nodiscard]] ParseResult parse_dds(
    std::span<const std::byte> source);

// Parse a PTX bundle through the Native Reader compatibility framing path and
// validate every framed DDS child. This remains one framing authority for both
// gallery browsing and model companion attachment. Result/diagnostic storage is
// intentionally throwing-capable below the module/Spider catch boundaries.
[[nodiscard]] ParseResult parse_ptx(
    std::span<const std::byte> source);

[[nodiscard]] const Slot* find_slot(
    const ParseResult& set,
    std::uint32_t index) noexcept;

[[nodiscard]] std::span<const std::byte> dds_bytes(
    std::span<const std::byte> source,
    const Slot& slot) noexcept;

// Decode only the requested slot. Callers decide whether/when to spend RGBA
// memory; parsing and slot validation therefore do not depend on gallery
// preview budgets. RGBA/detail construction may allocate.
// The image is mip 0 when it fits 4M pixels, else the first stored mip that
// fits, else mip 0 box-filtered; on success `detail` names the level used
// when it is not mip 0 at full size (UVs are unaffected).
[[nodiscard]] bool decode_base_mip(
    std::span<const std::byte> source,
    const Slot& slot,
    ImagePreview* out,
    std::string* detail = nullptr);

}  // namespace dmcresource::texture_set
