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

// What one script does, walked like the interpreter (first block chain up to
// the motion end, a loop jump or a hand-over to another MOT).
struct ScriptSummary final {
    std::uint8_t play_bank{};    // opcode 1 byte 4
    std::uint8_t play_index{};   // opcode 1 byte 5
    std::uint16_t instructions{};
    std::uint16_t waits{};       // opcode 0 blocks
    std::uint16_t last_frame{};  // highest wait frame below 0x7FFF
    bool loops{};                // opcode 2 (backward jump)
    bool hands_over{};           // a second opcode 1
    std::vector<WeaponStateKey> states;
    std::array<std::uint16_t, 64> opcodes{};  // count per opcode (0..63)
};

class MotionScriptFile final {
public:
    [[nodiscard]] static std::optional<MotionScriptFile> parse(std::span<const std::uint8_t> bytes);

    // Structural identity for a lone file: header table, 0xFFFF-terminated
    // bank list, and every non-empty bank's first script starts with opcode 1.
    [[nodiscard]] static bool looks_like(std::span<const std::uint8_t> bytes);

    [[nodiscard]] std::size_t bank_count() const noexcept { return banks_.size(); }
    [[nodiscard]] std::size_t size_bytes() const noexcept { return bytes_.size(); }
    [[nodiscard]] std::size_t header_table() const noexcept { return table_; }

    // Scripts in bank `bank` (entries before its 0xFFFF terminator).
    [[nodiscard]] std::size_t script_count(std::size_t bank) const noexcept;

    [[nodiscard]] std::optional<ScriptSummary> summarize(std::size_t bank, std::size_t index) const;

    // Weapon attach states of MOT `index` of bank `bank` (pl000_00_<bank>).
    [[nodiscard]] std::vector<WeaponStateKey> weapon_states(std::size_t bank,
                                                            std::size_t index) const;

private:
    std::vector<std::uint8_t> bytes_;
    std::vector<std::size_t> banks_;  // absolute bank table offsets
    std::size_t table_{};
};

// Weapon state at `frame` from a timeline (0 when none applies yet).
[[nodiscard]] std::uint8_t weapon_state_at(const std::vector<WeaponStateKey>& keys,
                                           float frame) noexcept;

// "pl000_00_<N>.pac" (any directory, any case) -> N.
[[nodiscard]] std::optional<std::size_t> player_motion_bank(std::string_view archive_name) noexcept;

}  // namespace dmcresource::motion
