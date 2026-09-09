#pragma once

#include <cstddef>
#include <span>

#include "dmc_rengine/profiles/dmc3/texture_slot_framing.hpp"

namespace dmcresource::ptx_compat {

namespace dmc3 = dmc::rengine::profiles::dmc3;

// Temporary Native Reader compatibility boundary for the pinned ReaderCore
// revision. This is not a second PTX parser: implementation retries the same
// canonical TextureSlotFramingParser only for the corpus-confirmed retail DXT1
// auxiliary-mode false negative. All physical framing/size/bounds authority
// remains in ReaderCore. Raw descriptor offsets stay private to the .cpp so
// callers cannot grow another format implementation around this workaround.
[[nodiscard]] dmc3::TextureSlotFramingResult parse_texture_bundle(
    std::span<const std::byte> source,
    bool* compatibility_used = nullptr);

}  // namespace dmcresource::ptx_compat
