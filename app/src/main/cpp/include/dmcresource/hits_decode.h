#pragma once

#include <cstddef>
#include <cstdint>

#include "dmcresource/decode.h"

namespace dmcresource {

// Decodes a DMC3 HITS collision resource into renderable triangle geometry.
//
// Structural authority: dmc-rengine-cpp `docs/formats/hits.md` and
// `src/formats/hits.cpp` (four-byte `HITS` magic, 0x44 header, 3-D spatial
// grid, 0x38 triangle/plane records, relative offsets based at +0x08).
DecodeResult decode_hits(const std::uint8_t* bytes, std::size_t size) noexcept;

}  // namespace dmcresource
