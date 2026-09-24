#pragma once

#include "dmcresource/image_preview.h"

// Neutral texture for geometry that has none of its own (a lone MOD / SCM /
// EFM, an assembled part without its PTX). The game keeps such a texture for
// its debug meshes: obj\debug\at.ptx in the system resource list 0x1405B0860,
// 128 x 64 flat grey 0x80 (the PS2 x1 modulation; the DXT5 file reads
// 0x7B..0x83). It is NOT shipped here: the same texture is generated in code,
// exact 0x80 grey, alpha 0xFF.
namespace dmcresource {

inline constexpr std::uint32_t kNeutralTextureWidth = 128U;
inline constexpr std::uint32_t kNeutralTextureHeight = 64U;
inline constexpr std::uint8_t kNeutralGrey = 0x80U;

[[nodiscard]] const ImagePreview& neutral_texture();

}  // namespace dmcresource
