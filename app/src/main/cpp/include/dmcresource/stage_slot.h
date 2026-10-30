#pragma once

#include <cstdint>

namespace dmcresource {

struct Session;

// The two sets of a stage's companions (effect bank, configuration):
// - base: the room of Settings that every model stands in;
// - scene: the stage archive opened as its own scene (⋮ Stage Ops).
// What one set holds never shows in the other. The active set is the one of
// the session being drawn or acted on (slot_for); the unqualified
// stage_room::effect_host / stage_markers::current read it.
enum class StageSlot : std::uint8_t { base = 0, scene = 1 };

// Scene for a session that carries a stage (a stage scene or a cutscene's
// stage), base otherwise.
[[nodiscard]] StageSlot slot_for(const Session* session) noexcept;
void set_active_slot(StageSlot slot) noexcept;
[[nodiscard]] StageSlot active_slot() noexcept;

}  // namespace dmcresource
