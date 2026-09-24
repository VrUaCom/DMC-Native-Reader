#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

// Read-only port of the IPlayer motion script (the byte code that drives each
// motion: pl000.pac slot 5). Reverse authority: dmc-rengine-cpp
// docs/research/dmc3-player-motion-script-2026-09-24.md.
//
// The player's script object (player+0x6A70 + form*0x240, init 0x1400594B0)
// takes the file from resource (type 0, character, slot 5). Header u16 +0 is
// the offset of a table whose first u16 leads to the bank list (u16 offsets,
// 0xFFFF-terminated, one bank per motion\\pl000\\pl000_00_N.pac); each bank
// is a u16 offset list, one script per MOT index (lookup 0x14005A290).
// Interpreter 0x140058FE0 runs a block until opcode 0 ([00, ?, frame u16,
// flag, ?]: the next block runs once the motion frame passes `frame`, 0x7FFF =
// motion end). Opcode 3 ([03, b1..b5]) sets channel 0; 0x1401F01F0 reads b2:
// low 6 bits = weapon attach state (0 = unchanged), high bits = flags.
namespace dmcresource::motion {

// State `state` applies once the motion frame is past `after_frame`
// (-1: from the first frame).
struct WeaponStateKey final {
    float after_frame{-1.0F};
    std::uint8_t state{};
};

class MotionScriptFile final {
public:
    [[nodiscard]] static std::optional<MotionScriptFile> parse(std::span<const std::uint8_t> bytes);

    [[nodiscard]] std::size_t bank_count() const noexcept { return banks_.size(); }

    // Weapon attach states of MOT `index` of bank `bank` (pl000_00_<bank>).
    [[nodiscard]] std::vector<WeaponStateKey> weapon_states(std::size_t bank,
                                                            std::size_t index) const;

private:
    std::vector<std::uint8_t> bytes_;
    std::vector<std::size_t> banks_;  // absolute bank table offsets
};

// Weapon state at `frame` from a timeline (0 when none applies yet).
[[nodiscard]] std::uint8_t weapon_state_at(const std::vector<WeaponStateKey>& keys,
                                           float frame) noexcept;

// "pl000_00_<N>.pac" (any directory, any case) -> N.
[[nodiscard]] std::optional<std::size_t> player_motion_bank(std::string_view archive_name) noexcept;

}  // namespace dmcresource::motion
