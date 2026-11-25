#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "dmcresource/motion/cloth_chain.h"
#include "dmcresource/motion/motion_script.h"

// The coat's motion states of a player archive (slot 10, 2 KiB, after the
// ff&A table): the parameters the player object gives its coat cloth when the
// motion script asks for another state. Research
// docs/research/dmc3-player-bin-slots-exe.md:
// - the player init (0x140225D70) keeps slot 10 at object +0x3DF0 (slot 9 at
//   +0x3DE8, slot 11 at +0x3DF8);
// - 0x1401F01F0 reads the opcode-3 signal each frame; channel 3 is the coat
//   state (+0x39C5);
// - 0x1401FBD00 / 0x1401FBD90, when the state is non-zero and changed, take
//   entry `state` of the table (u16 pairs, the second the record's offset)
//   and write the record's five floats into the coat cloth: stiffness, a
//   vertical gravity and a wind vector.
namespace dmcresource::motion {

// The slot of a player archive that holds the coat states.
inline constexpr std::uint32_t kPlayerCoatStatesSlot = 10U;

struct CoatStateRecord final {
    std::uint16_t group{};   // first half of the entry (0x5C / 0x70; not read by the consumers)
    std::uint16_t offset{};  // the record's offset in the table
    float stiffness{};       // float 0
    float gravity_y{};       // float 1 -> gravity (0, y, 0)
    std::array<float, 3> wind{};  // floats 2..4
};

class CoatStates final {
public:
    // The table, or nothing when the bytes are not one: an entry list whose
    // halves all point inside the table at its data, each record whole.
    [[nodiscard]] static std::optional<CoatStates> parse(std::span<const std::uint8_t> bytes);

    [[nodiscard]] std::size_t count() const noexcept { return records_.size(); }
    [[nodiscard]] const std::vector<CoatStateRecord>& records() const noexcept { return records_; }
    // Entry `state` (the script's channel 3), nullptr when out of range.
    [[nodiscard]] const CoatStateRecord* record(std::size_t state) const noexcept {
        return state < records_.size() ? &records_[state] : nullptr;
    }

private:
    std::vector<CoatStateRecord> records_;
};

// The coat state the script has set by `frame`: the last opcode-3 key passed
// whose channel 3 is non-zero (the game only acts on a non-zero change), 0
// when none has (the .clt's own parameters stand).
[[nodiscard]] int coat_state_at(std::span<const ScriptSignalKey> keys, float frame) noexcept;

// The cloth parameters of state `record` on top of the .clt's `base`.
[[nodiscard]] ClothParams coat_params(const ClothParams& base, const CoatStateRecord& record) noexcept;

}  // namespace dmcresource::motion
