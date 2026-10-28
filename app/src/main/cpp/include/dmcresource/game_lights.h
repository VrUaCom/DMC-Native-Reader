#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>

// The game's light table (LIG2): 48 point / directional light records, the
// choice of the four lights that light a query point, and the colour and
// vector matrices a renderer uploads for them, and a model's three-light rig
// built from those matrices. Written clean-room from the
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
    std::int8_t static_light_count{};             // +0x04 as signed 8-bit: lights a model keeps
    std::array<float, 3> ambient{};               // u16 at +0x0C, +0x0E, +0x10 as floats, unscaled
    std::array<float, 3> ambient_mask2{};         // u16 at +0x06, +0x08, +0x0A: ambient when mask == 2
};

// Which light list a selection fills: main needs mask bit 4, secondary bit 8.
enum class ListKind : std::uint8_t { main, secondary };

// Null when shorter than 0x920 bytes, the magic is not "LIG2", or an active
// record has a non-finite position, aux_w or falloff; extra bytes are ignored.
[[nodiscard]] std::optional<LightTable> read_light_table(std::span<const std::uint8_t> bytes) noexcept;
// The table at the first 4-aligned offset where read_light_table succeeds.
[[nodiscard]] std::optional<LightTable> find_light_table(std::span<const std::uint8_t> bytes) noexcept;

// Candidate: active, no colour modifier, type 0 or 1, the list's mask bit set,
// and, with f = flags (secondary: bit 3 cleared), either f & 4 with mask & 0x10,
// or low = f without bit 2 allowed: 1 needs mask & 3, 2 mask & 2, 3 mask & 1.
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

// Candidates in record order enter slot 3 only when scoring strictly above it,
// then rise past every slot scoring <= theirs (a later equal score goes above).
// With single given and mask & 2, it is scored and put in slot 0, shifting the
// others down (slot 3 dropped). lc row: (r, g, b, falloff | 1 for type 1);
// lv row: (position - query, aux_w | 0 for type 1). Unused rows zero, index -1.
[[nodiscard]] SelectedLights select_lights(const LightTable& table, std::array<float, 3> query,
                                           unsigned mask, ListKind list = ListKind::main,
                                           const LightRecord* single = nullptr) noexcept;

struct ModelRig {
    int count{};                                         // 0..3 lights
    std::array<std::array<float, 3>, 3> direction{};     // unit vectors
    std::array<std::array<float, 3>, 3> colour{};        // 0..255 units (128 = 1)
    std::array<float, 3> ambient{};                      // 0..255 units
};

// Uses the first min(lights.count, 3, max(limit, 0)) slots. Per slot: attenuation
// a = max(0, 1 - |lv.xyz|^2 * lv.w) when lv.w != 0, else 1; colour = lc.rgb * a
// (score and lc.w unused); direction = lv.xyz normalised (zero stays zero).
// Ambient is table.ambient_mask2 when mask == 2 exactly, else table.ambient.
[[nodiscard]] ModelRig model_rig(const LightTable& table, const SelectedLights& lights,
                                 unsigned mask, int limit = 3) noexcept;

// The number of records that pass the candidate rules (no score applied).
[[nodiscard]] std::size_t candidate_count(const LightTable& table, unsigned mask, ListKind list) noexcept;
// One line: "LIG2 v3: 9 of 48 records active (type 0: 6, type 1: 3; 2 with a
// colour modifier)"; counts are over active records. Empty on allocation failure.
[[nodiscard]] std::string describe(const LightTable& table) noexcept;

}  // namespace dmcresource::game_lights
