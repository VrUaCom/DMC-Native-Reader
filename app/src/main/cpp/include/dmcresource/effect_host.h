#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include "dmcresource/resource_session.h"

namespace dmcresource {

// Parses an FXBANK (PNST manifest + records) and decodes its T textures once,
// as PAC assembly keeps them for character effects. `slot` is the provenance
// number the effect children use to find this bank.
[[nodiscard]] std::optional<Session::EffectBank> load_effect_bank(
    std::shared_ptr<const std::vector<std::uint8_t>> source, std::uint32_t slot);

}  // namespace dmcresource
