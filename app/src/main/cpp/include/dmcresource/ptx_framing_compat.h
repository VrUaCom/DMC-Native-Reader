#pragma once

#include <cstddef>
#include <span>

#include "dmc_rengine/profiles/dmc3/texture_slot_framing_compat.hpp"

namespace dmcresource::ptx_compat {

namespace dmc3 = dmc::rengine::profiles::dmc3;

// Product-side facade over DMC Rengine's canonical read compatibility layer.
// Native Reader owns no descriptor constants or legacy framing grammar here.
// Retail PTX bundles whose DDS children hold only the base level (no mip
// chain), e.g. the interface textures of id*.pac, are outside the canonical
// full-chain descriptor domain; they are read by a strict single-level path
// and reported through single_level_used.
// Bundles whose DDS children are BC1..BC7 beyond retail DXT1/DXT5 (BC2,
// BC4..BC7, or any DX10 header) are read with the checks dmc3.exe itself
// makes when it loads a texture, reported through extended_formats_used.
// For those entries TextureSlotEntry::compression only holds the block size
// class (dxt1 = 8-byte blocks, dxt5 = 16-byte blocks); the real format comes
// from the DDS header.
[[nodiscard]] dmc3::TextureSlotFramingResult parse_texture_bundle(
    std::span<const std::byte> source,
    bool* compatibility_used = nullptr,
    bool* community_descriptors_used = nullptr,
    bool* single_level_used = nullptr,
    bool* extended_formats_used = nullptr);

}  // namespace dmcresource::ptx_compat
