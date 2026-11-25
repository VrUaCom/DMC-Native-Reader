#pragma once

// PTX texture bundles holding uncompressed DDS. Some character PACs of the HD
// collection's GData.afs (pl000, em034 slot 0) keep one texture of their PTX
// as a plain 32-bit (or 24-bit) RGB(A) DDS beside the DXT1 / DXT5 ones. The
// core's PTX readers, its texture set and the GPU path take block formats
// only, so the whole bundle was rejected and the model drew grey. The core
// rewrites such a bundle when it opens it: each uncompressed texture is
// encoded as BC3 (DXT5, alpha kept) with the same number of mip levels, the
// block textures are kept byte for byte, and the bundle is laid out again in
// 0x800-byte sectors (count, per-texture sector spans, 0x70-byte descriptor
// with the DDS size at +0x64, the DDS, zero padding).

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace dmcresource::ptx_rgba {

// The bundle with its uncompressed textures encoded as BC3; nullopt when it
// is no PTX bundle of this layout or has no uncompressed texture.
[[nodiscard]] std::optional<std::vector<std::uint8_t>> compress(std::span<const std::uint8_t> bundle);

}  // namespace dmcresource::ptx_rgba
