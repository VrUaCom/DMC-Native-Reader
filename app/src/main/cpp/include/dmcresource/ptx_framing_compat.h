#pragma once

#include <cstddef>
#include <span>

#include "dmc_rengine/profiles/dmc3/texture_slot_framing_compat.hpp"

namespace dmcresource::ptx_compat {

namespace dmc3 = dmc::rengine::profiles::dmc3;

// Product-side facade over DMC Rengine's canonical read compatibility layer.
// Native Reader owns no descriptor constants or legacy framing grammar here.
[[nodiscard]] dmc3::TextureSlotFramingResult parse_texture_bundle(
    std::span<const std::byte> source,
    bool* compatibility_used = nullptr);

}  // namespace dmcresource::ptx_compat
