#include "dmcresource/game_material.h"

// Every rule lives constexpr in the header; this unit pins the material byte
// layout at compile time.
namespace dmcresource::game_material {

static_assert(material_byte(0x0Fu | 0x4000u, 0xFF) <= 0x3F,
              "material byte bits 6..7 stay zero");
static_assert(material_part_alpha(material_byte(0, 0xC1)) == 1.0f);

} // namespace dmcresource::game_material
