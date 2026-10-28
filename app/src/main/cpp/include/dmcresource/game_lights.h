#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>

// The game's light table (LIG2): 48 point / directional light records, the
// choice of the four lights that light a query point, and the colour and
// vector matrices a renderer uploads for them. Written clean-room from the
// behaviour specification docs/cleanroom/specs-v124/game_lights.md.
namespace dmcresource::game_lights {

inline constexpr std::size_t kRecordCount = 48;
inline constexpr std::size_t kHeaderSize = 0x20;
inline constexpr std::size_t kRecordSize = 0x30;
inline constexpr std::size_t kTableSize = kHeaderSize + kRecordCount * kRecordSize;  // 0x920

struct LightRecord {
    std::uint8_t index{};          // 0..47
    bool active{};                 // any of the record's 0x30 bytes is non-zero
    std::uint8_t flags{}, colour_modifier{}, type{};
    std::array<float, 3> position{};
    float aux_w{}, falloff{};
    std::array<std::uint16_t, 3> rgb{};
};

struct LightTable {
    std::uint16_t version{};
    std::array<std::uint16_t, 6> header_words{};  // meaning unknown; for display
    std::array<LightRecord, kRecordCount> records{};
};

// Which light list a selection fills: main needs mask bit 4, secondary bit 8.
enum class ListKind : std::uint8_t { main, secondary };

// Null when shorter than 0x920 bytes, the magic is not "LIG2", or an active
// record has a non-finite position, aux_w or falloff; extra bytes are ignored.
[[nodiscard]] std::optional<LightTable> read_light_table(std::span<const std::uint8_t> bytes) noexcept;
// The table at the first 4-aligned offset where read_light_table succeeds.
[[nodiscard]] std::optional<LightTable> find_light_table(std::span<const std::uint8_t> bytes) noexcept;

// Candidate: active, no colour modifier, type 0 or 1, the list's mask bit set,
// and the low mode (flags without bit 2; secondary also without bit 3)
// allowed: 1 needs mask & 3, 2 needs mask & 2, 3 needs mask & 1, else never.
[[nodiscard]] bool is_candidate(const LightRecord& record, unsigned mask, ListKind list) noexcept;
// Type 0: max(0, 1 - |position - query|^2 * falloff); type 1: 1.
[[nodiscard]] float score(const LightRecord& record, std::array<float, 3> query) noexcept;

struct SelectedLights {
    int count{};                                  // 0..4
    std::array<int, 4> index{-1, -1, -1, -1};     // record index per slot
    std::array<float, 4> score{};
    std::array<float, 16> lc{};                   // 4 rows of 4: colour
    std::array<float, 16> lv{};                   // 4 rows of 4: vector
};

// Up to four candidates with score > 0, highest score first, equal scores
// higher index first. lc row: (r, g, b, falloff | 1 for type 1); lv row:
// (position - query, aux_w | 0 for type 1). Unused rows zero, index -1.
[[nodiscard]] SelectedLights select_lights(const LightTable& table, std::array<float, 3> query,
                                           unsigned mask, ListKind list = ListKind::main) noexcept;

// The number of records that pass the candidate rules (no score applied).
[[nodiscard]] std::size_t candidate_count(const LightTable& table, unsigned mask, ListKind list) noexcept;
// One line: "LIG2 v3: 9 of 48 records active (type 0: 6, type 1: 3; 2 with a
// colour modifier)"; counts are over active records. Empty on allocation failure.
[[nodiscard]] std::string describe(const LightTable& table) noexcept;

}  // namespace dmcresource::game_lights
