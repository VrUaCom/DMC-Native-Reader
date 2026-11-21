#pragma once

// Which objects of a player's body model are drawn, as the game decides it.
// Every MOD object carries a draw bit (object +0x00 bit 0; the draw loops
// skip an object without it). The player classes set and clear it:
//
// CPlDante (pl000 and its costumes pl011, pl013, pl015, pl016, pl018):
//   * hands, 0x1402169F0: objects 0 and 1 are the two hand / arm meshes.
//     The motion script's opcode 3 byte b3 picks one when its bit 0x40 is
//     set (0x1401F01F0 stores it; every new action clears it, 0x1401F0080):
//     b3 & 0x3F == 0 (or no such byte yet) draws object 0, == 1 draws 1,
//     other values leave the hands as they are.
//   * Beowulf, 0x140212BE0 (equipping): with Beowulf (melee weapon 4) the
//     forearms and shins are its gauntlets and greaves: objects 4 and 5
//     drawn, 3 and 6 not; without it 3 and 6 drawn, 4 and 5 not.
//
// CPlNewVergil (pl021, pl023, pl026), 0x140223420:
//   * hands: objects 2 and 4, picked by the same b3 rule (0 -> 2, 1 -> 4);
//   * with Beowulf (instance +0x6484 == 1) object 4 and objects 7 and 9 are
//     drawn, 2, 8 and 10 not; without it 8 and 10 drawn, 7 and 9 not.
//
// Other b3 bits of opcode 3 are not visual: 0x20 starts a controller
// rumble (table 0x14058B420, 3 bytes per entry), 0x10 a camera shake
// (0x140325970). The core does not draw them.

#include <cstdint>
#include <memory>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

#include "dmcresource/motion/motion_script.h"

namespace dmcresource {
struct Session;
}

namespace dmcresource::motion {

enum class VisibilityRule : std::uint8_t {
    none,
    dante,
    new_vergil,
};

struct ObjectVisibility final {
    VisibilityRule rule{VisibilityRule::none};
    // Render-mesh vertex range [first, second) of each object of the body
    // model, by object index (empty range: no such object).
    std::vector<std::pair<std::uint32_t, std::uint32_t>> object_ranges;
    // The full triangle list and its texture slots, before any object is hidden.
    std::vector<std::uint32_t> all_indices;
    std::vector<std::uint32_t> all_slots;
    std::vector<bool> hidden;  // by object index
    int hand_variant{0};       // 0 or 1 (the b3 value)
    bool beowulf{};            // Beowulf equipped
};

// The rule of a player archive by its name ("pl000.pac", any directory/case).
[[nodiscard]] VisibilityRule visibility_rule_for(std::string_view archive_name) noexcept;

// Installs the rule on an assembled player session whose body model is
// composite part `body_part`, and draws its default objects (hand 0, no
// Beowulf). False when the rule or the part does not apply.
bool install_object_visibility(Session* session, std::size_t body_part, VisibilityRule rule);

// The hand variant at `frame` from the opcode-3 five-channel timeline:
// b3 (channel 2) with bit 0x40 and value 0 or 1, the last one passed.
[[nodiscard]] int hand_variant_at(std::span<const ScriptSignalKey> keys, float frame) noexcept;

// Draws the objects of hand variant `variant` (and the Beowulf state);
// rebuilds the triangle list only when something changed.
void set_hand_variant(Session* session, int variant);
void set_beowulf(Session* session, bool equipped);

// Object indices the session draws now / hides now, for Info and tests.
[[nodiscard]] std::vector<std::uint32_t> hidden_objects(const Session* session);

}  // namespace dmcresource::motion
