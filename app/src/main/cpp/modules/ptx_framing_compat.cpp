#include "dmcresource/ptx_framing_compat.h"

#include <algorithm>

namespace dmcresource::ptx_compat {

namespace dmc3 = dmc::rengine::profiles::dmc3;

dmc3::TextureSlotFramingResult parse_texture_bundle(
    std::span<const std::byte> source,
    bool* compatibility_used) {
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
    return read.framing;
}

}  // namespace dmcresource::ptx_compat
