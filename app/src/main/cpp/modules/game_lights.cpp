// Game light tables (LIG2): reading, candidate rules, top-four selection and
// the light matrices. See dmcresource/game_lights.h.
#include "dmcresource/game_lights.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>

namespace dmcresource::game_lights {
namespace {

// Little-endian reads through memcpy (no unaligned pointer casts).
template <typename T>
T read_le(std::span<const std::uint8_t> bytes, std::size_t offset) noexcept {
    T value{};
    std::memcpy(&value, bytes.data() + offset, sizeof value);
    if constexpr (std::endian::native == std::endian::big) value = std::byteswap(value);
    return value;
}

std::uint16_t read_u16(std::span<const std::uint8_t> bytes, std::size_t offset) noexcept {
    return read_le<std::uint16_t>(bytes, offset);
}

float read_f32(std::span<const std::uint8_t> bytes, std::size_t offset) noexcept {
    return std::bit_cast<float>(read_le<std::uint32_t>(bytes, offset));
}

std::array<float, 3> delta_of(const LightRecord& record, std::array<float, 3> query) noexcept {
    return {record.position[0] - query[0], record.position[1] - query[1], record.position[2] - query[2]};
}

}  // namespace

std::optional<LightTable> read_light_table(std::span<const std::uint8_t> bytes) noexcept {
    if (bytes.size() < kTableSize) return std::nullopt;
    if (bytes[0] != 'L' || bytes[1] != 'I' || bytes[2] != 'G' || bytes[3] != '2') return std::nullopt;

    LightTable table{};
    table.version = read_u16(bytes, 0x04);
    for (std::size_t i = 0; i < table.header_words.size(); ++i) {
        table.header_words[i] = read_u16(bytes, 0x06 + i * 2);
    }

    for (std::size_t i = 0; i < kRecordCount; ++i) {
        const auto rec = bytes.subspan(kHeaderSize + i * kRecordSize, kRecordSize);
        LightRecord& r = table.records[i];
        r.index = static_cast<std::uint8_t>(i);
        r.active = std::any_of(rec.begin(), rec.end(), [](std::uint8_t b) { return b != 0; });
        r.flags = rec[0x00];
        r.colour_modifier = rec[0x01];
        r.type = rec[0x02];
        r.position = {read_f32(rec, 0x10), read_f32(rec, 0x14), read_f32(rec, 0x18)};
        r.aux_w = read_f32(rec, 0x1C);
        r.falloff = read_f32(rec, 0x20);
        r.rgb = {read_u16(rec, 0x24), read_u16(rec, 0x26), read_u16(rec, 0x28)};

        if (r.active) {
            const bool finite = std::isfinite(r.position[0]) && std::isfinite(r.position[1]) &&
                                std::isfinite(r.position[2]) && std::isfinite(r.aux_w) &&
                                std::isfinite(r.falloff);
            if (!finite) return std::nullopt;
        }
    }
    return table;
}

std::optional<LightTable> find_light_table(std::span<const std::uint8_t> bytes) noexcept {
    for (std::size_t offset = 0; offset + kTableSize <= bytes.size(); offset += 4) {
        if (auto table = read_light_table(bytes.subspan(offset))) return table;
    }
    return std::nullopt;
}

bool is_candidate(const LightRecord& record, unsigned mask, ListKind list) noexcept {
    if (!record.active) return false;
    if (record.colour_modifier != 0) return false;
    if (record.type > 1) return false;
    const unsigned list_bit = list == ListKind::main ? 4u : 8u;
    if ((mask & list_bit) == 0) return false;

    unsigned f = record.flags;
    if (list == ListKind::secondary) f &= 0xF7u;
    const unsigned low = f & ~4u;
    switch (low) {
        case 1: return (mask & 3u) != 0;
        case 2: return (mask & 2u) != 0;
        case 3: return (mask & 1u) != 0;
        default: return false;
    }
}

float score(const LightRecord& record, std::array<float, 3> query) noexcept {
    if (record.type == 1) return 1.0f;
    const auto d = delta_of(record, query);
    const float dot = d[0] * d[0] + d[1] * d[1] + d[2] * d[2];
    return std::max(0.0f, 1.0f - dot * record.falloff);
}

SelectedLights select_lights(const LightTable& table, std::array<float, 3> query, unsigned mask,
                             ListKind list) noexcept {
    struct Pick {
        int index;
        float score;
    };
    std::array<Pick, kRecordCount> picks{};
    std::size_t n = 0;
    for (const LightRecord& r : table.records) {
        if (!is_candidate(r, mask, list)) continue;
        const float s = score(r, query);
        if (!(s > 0.0f)) continue;
        picks[n++] = {r.index, s};
    }
    std::sort(picks.begin(), picks.begin() + static_cast<std::ptrdiff_t>(n), [](const Pick& a, const Pick& b) {
        if (a.score != b.score) return a.score > b.score;
        return a.index > b.index;
    });

    SelectedLights out{};
    out.count = static_cast<int>(std::min<std::size_t>(n, 4));
    for (int slot = 0; slot < out.count; ++slot) {
        const LightRecord& r = table.records[static_cast<std::size_t>(picks[slot].index)];
        const auto d = delta_of(r, query);
        const std::size_t row = static_cast<std::size_t>(slot) * 4;
        out.index[slot] = picks[slot].index;
        out.score[slot] = picks[slot].score;
        out.lc[row + 0] = static_cast<float>(r.rgb[0]);
        out.lc[row + 1] = static_cast<float>(r.rgb[1]);
        out.lc[row + 2] = static_cast<float>(r.rgb[2]);
        out.lc[row + 3] = r.type == 0 ? r.falloff : 1.0f;
        out.lv[row + 0] = d[0];
        out.lv[row + 1] = d[1];
        out.lv[row + 2] = d[2];
        out.lv[row + 3] = r.type == 0 ? r.aux_w : 0.0f;
    }
    return out;
}

std::size_t candidate_count(const LightTable& table, unsigned mask, ListKind list) noexcept {
    return static_cast<std::size_t>(std::count_if(table.records.begin(), table.records.end(),
                                                  [&](const LightRecord& r) { return is_candidate(r, mask, list); }));
}

std::string describe(const LightTable& table) noexcept {
    std::size_t active = 0, type0 = 0, type1 = 0, modified = 0;
    for (const LightRecord& r : table.records) {
        if (!r.active) continue;
        ++active;
        if (r.type == 0) ++type0;
        if (r.type == 1) ++type1;
        if (r.colour_modifier != 0) ++modified;
    }
    try {
        return "LIG2 v" + std::to_string(table.version) + ": " + std::to_string(active) + " of " +
               std::to_string(kRecordCount) + " records active (type 0: " + std::to_string(type0) +
               ", type 1: " + std::to_string(type1) + "; " + std::to_string(modified) +
               " with a colour modifier)";
    } catch (...) {
        return {};
    }
}

}  // namespace dmcresource::game_lights
