#pragma once

// Archives whose files are stored zlib-compressed. Many PAC and PNST files of
// the HD collection's GData.afs hold every slot as a zlib stream (RFC 1950),
// while the files inside, once inflated, are the ordinary ones (MOD, PAC,
// textures, motions). The core inflates such an archive into the ordinary
// layout before anything reads it, so assembling, stages and every file view
// work on it unchanged.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace dmcresource::slot_inflate {

// One zlib stream (RFC 1950 header, RFC 1951 data, Adler-32 checked), at most
// `limit` bytes out. Bytes after the stream are ignored. nullopt when the
// bytes are not a whole, valid stream or it would exceed `limit`.
[[nodiscard]] std::optional<std::vector<std::uint8_t>> inflate_zlib(std::span<const std::uint8_t> stream,
                                                                    std::size_t limit);

// True when the first two bytes are a zlib header (deflate, 32 KiB window or
// less, no preset dictionary, header check passes).
[[nodiscard]] bool looks_like_zlib(std::span<const std::uint8_t> bytes) noexcept;

// A PAC or PNST archive with at least one zlib-compressed slot (or one PTX
// slot with uncompressed textures), rebuilt with
// every such slot inflated (the others copied), the slots aligned as the
// game's own archives are; a slot that only starts like zlib is kept as it is.
// nullopt when it is no such archive, nothing was inflated, or the result
// would exceed the core's resource limit.
// What expand changed: slots inflated, PTX bundles whose uncompressed
// textures were encoded as BC3 (ptx_rgba.h).
struct Expanded final {
    std::size_t inflated{};
    std::size_t textures_compressed{};
};

// Also rewrites a slot (inflated or stored) that is a PTX bundle with
// uncompressed textures (ptx_rgba::compress), so such an archive is rebuilt
// even when nothing in it is zlib-compressed.
[[nodiscard]] std::optional<std::vector<std::uint8_t>> expand(std::span<const std::uint8_t> archive,
                                                              Expanded* what = nullptr);

}  // namespace dmcresource::slot_inflate
