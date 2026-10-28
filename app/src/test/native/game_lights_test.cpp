// Game light tables (game_lights.h): reading LIG2 tables, the candidate rules
// per list and mask, the score, the top-four order and the lc / lv matrices.
#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <span>
#include <vector>

#include "dmcresource/game_lights.h"

using namespace dmcresource::game_lights;

namespace {

struct RecordSpec {
    std::uint8_t flags{};
    std::uint8_t colour_modifier{};
    std::uint8_t type{};
    std::array<float, 3> position{};
    float aux_w{};
    float falloff{};
    std::array<std::uint16_t, 3> rgb{};
};

void put_u16(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint16_t value) {
    bytes[offset] = static_cast<std::uint8_t>(value & 0xFF);
    bytes[offset + 1] = static_cast<std::uint8_t>(value >> 8);
}

void put_f32(std::vector<std::uint8_t>& bytes, std::size_t offset, float value) {
    std::uint32_t bits{};
    std::memcpy(&bits, &value, sizeof bits);
    for (std::size_t i = 0; i < 4; ++i) bytes[offset + i] = static_cast<std::uint8_t>(bits >> (8 * i));
}

// Writes a LIG2 table: header, then 48 records of 0x30 bytes (all zero unless set).
struct TableBytes {
    std::vector<std::uint8_t> bytes = std::vector<std::uint8_t>(kTableSize, 0);

    explicit TableBytes(std::uint16_t version = 3) {
        bytes[0] = 'L';
        bytes[1] = 'I';
        bytes[2] = 'G';
        bytes[3] = '2';
        put_u16(bytes, 0x04, version);
    }

    TableBytes& set(std::size_t index, const RecordSpec& spec) {
        const std::size_t base = kHeaderSize + index * kRecordSize;
        bytes[base + 0x00] = spec.flags;
        bytes[base + 0x01] = spec.colour_modifier;
        bytes[base + 0x02] = spec.type;
        put_f32(bytes, base + 0x10, spec.position[0]);
        put_f32(bytes, base + 0x14, spec.position[1]);
        put_f32(bytes, base + 0x18, spec.position[2]);
        put_f32(bytes, base + 0x1C, spec.aux_w);
        put_f32(bytes, base + 0x20, spec.falloff);
        put_u16(bytes, base + 0x24, spec.rgb[0]);
        put_u16(bytes, base + 0x26, spec.rgb[1]);
        put_u16(bytes, base + 0x28, spec.rgb[2]);
        return *this;
    }

    [[nodiscard]] LightTable read() const {
        auto table = read_light_table(bytes);
        assert(table.has_value());
        return *table;
    }
};

bool near(float a, float b, float eps = 1e-6f) { return std::fabs(a - b) <= eps; }

// --- reading -----------------------------------------------------------------

void test_read_fields() {
    TableBytes t(7);
    for (std::size_t i = 0; i < 6; ++i) put_u16(t.bytes, 0x06 + i * 2, static_cast<std::uint16_t>(0x100 + i));
    t.set(4, {.flags = 0x05, .colour_modifier = 2, .type = 1, .position = {1.5f, -2.0f, 3.25f},
              .aux_w = 4.0f, .falloff = 0.125f, .rgb = {10, 20, 0xFFFF}});
    const LightTable table = t.read();
    assert(table.version == 7);
    for (std::size_t i = 0; i < 6; ++i) assert(table.header_words[i] == 0x100 + i);
    for (std::size_t i = 0; i < kRecordCount; ++i) {
        assert(table.records[i].index == i);
        assert(table.records[i].active == (i == 4));
    }
    const LightRecord& r = table.records[4];
    assert(r.flags == 0x05 && r.colour_modifier == 2 && r.type == 1);
    assert(r.position[0] == 1.5f && r.position[1] == -2.0f && r.position[2] == 3.25f);
    assert(r.aux_w == 4.0f && r.falloff == 0.125f);
    assert(r.rgb[0] == 10 && r.rgb[1] == 20 && r.rgb[2] == 0xFFFF);
}

void test_read_size_and_magic() {
    TableBytes t;
    assert(read_light_table(t.bytes).has_value());

    std::vector<std::uint8_t> shorter(t.bytes.begin(), t.bytes.end() - 1);
    assert(!read_light_table(shorter).has_value());
    assert(!read_light_table({}).has_value());

    std::vector<std::uint8_t> longer = t.bytes;
    longer.resize(kTableSize + 100, 0xAB);
    assert(read_light_table(longer).has_value());

    for (std::size_t i = 0; i < 4; ++i) {
        TableBytes bad;
        bad.bytes[i] ^= 0x20;
        assert(!read_light_table(bad.bytes).has_value());
    }
}

void test_read_non_finite() {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    const RecordSpec ok{.flags = 1, .type = 0, .position = {1, 2, 3}, .aux_w = 1, .falloff = 0.1f};

    for (std::size_t field = 0; field < 5; ++field) {
        for (float bad : {nan, inf, -inf}) {
            RecordSpec spec = ok;
            if (field < 3) spec.position[field] = bad;
            if (field == 3) spec.aux_w = bad;
            if (field == 4) spec.falloff = bad;
            TableBytes t;
            t.set(10, spec);
            assert(!read_light_table(t.bytes).has_value());
        }
    }

    // A non-finite float has non-zero bytes, so its record is active: an
    // inactive (all zero) record only holds zeros, which are finite.
    TableBytes only_nan;
    only_nan.set(20, {.position = {nan, 0, 0}});
    assert(!read_light_table(only_nan.bytes).has_value());

    const LightTable empty = TableBytes().read();
    for (const LightRecord& r : empty.records) {
        assert(!r.active);
        assert(r.position[0] == 0 && r.position[1] == 0 && r.position[2] == 0);
        assert(r.aux_w == 0 && r.falloff == 0);
    }
}

void test_find_light_table() {
    TableBytes t(5);
    t.set(1, {.flags = 3, .type = 1, .rgb = {1, 2, 3}});
    std::vector<std::uint8_t> container(0x40, 0x11);
    container.insert(container.end(), t.bytes.begin(), t.bytes.end());
    container.resize(container.size() + 0x30, 0x22);

    const auto found = find_light_table(container);
    assert(found.has_value());
    assert(found->version == 5);
    assert(found->records[1].active && found->records[1].rgb[2] == 3);

    std::vector<std::uint8_t> none(0x1000, 0x11);
    assert(!find_light_table(none).has_value());
    assert(!find_light_table({}).has_value());
}

// --- eligibility -------------------------------------------------------------

constexpr std::array<unsigned, 8> kMasks{1, 2, 3, 4, 5, 6, 7, 11};

// Selected for (flags, list) on a type 1 light, per mask in kMasks.
std::array<bool, 8> selected_per_mask(std::uint8_t flags, ListKind list, std::uint8_t type = 1,
                                      std::uint8_t modifier = 0) {
    TableBytes t;
    t.set(0, {.flags = flags, .colour_modifier = modifier, .type = type, .position = {0, 0, 0},
              .falloff = 0.0f, .rgb = {1, 1, 1}});
    const LightTable table = t.read();
    std::array<bool, 8> out{};
    for (std::size_t i = 0; i < kMasks.size(); ++i) {
        const auto sel = select_lights(table, {0, 0, 0}, kMasks[i], list);
        const std::size_t count = candidate_count(table, kMasks[i], list);
        assert(count == static_cast<std::size_t>(sel.count));
        out[i] = sel.count == 1;
        if (out[i]) assert(sel.index[0] == 0);
    }
    return out;
}

using Row = std::array<bool, 8>;
constexpr bool o = true, x = false;

void test_eligibility_main() {
    //                       masks: 1  2  3  4  5  6  7  11
    assert(selected_per_mask(0, ListKind::main) == (Row{x, x, x, x, x, x, x, x}));
    assert(selected_per_mask(1, ListKind::main) == (Row{x, x, x, x, o, o, o, x}));
    assert(selected_per_mask(2, ListKind::main) == (Row{x, x, x, x, x, o, o, x}));
    assert(selected_per_mask(3, ListKind::main) == (Row{x, x, x, x, o, x, o, x}));
    assert(selected_per_mask(8, ListKind::main) == (Row{x, x, x, x, x, x, x, x}));
    assert(selected_per_mask(0x10, ListKind::main) == (Row{x, x, x, x, x, x, x, x}));
    // Bit 2 is ignored: 4 is 0, 5 is 1, 6 is 2, 7 is 3.
    for (std::uint8_t low = 0; low < 4; ++low) {
        assert(selected_per_mask(low | 4, ListKind::main) == selected_per_mask(low, ListKind::main));
    }
    // Bit 3 is only cleared for the secondary list.
    assert(selected_per_mask(0x0B, ListKind::main) == (Row{x, x, x, x, x, x, x, x}));
}

void test_eligibility_secondary() {
    //                       masks: 1  2  3  4  5  6  7  11
    assert(selected_per_mask(0, ListKind::secondary) == (Row{x, x, x, x, x, x, x, x}));
    assert(selected_per_mask(1, ListKind::secondary) == (Row{x, x, x, x, x, x, x, o}));
    assert(selected_per_mask(2, ListKind::secondary) == (Row{x, x, x, x, x, x, x, o}));
    assert(selected_per_mask(3, ListKind::secondary) == (Row{x, x, x, x, x, x, x, o}));
    assert(selected_per_mask(7, ListKind::secondary) == selected_per_mask(3, ListKind::secondary));
    assert(selected_per_mask(0x0B, ListKind::secondary) == selected_per_mask(3, ListKind::secondary));

    // Beyond kMasks: the low mode still decides under the secondary bit.
    TableBytes t;
    t.set(0, {.flags = 0x0B, .type = 1});  // like 3: needs mask & 1
    t.set(1, {.flags = 0x02, .type = 1});  // needs mask & 2
    t.set(2, {.flags = 0x01, .type = 1});  // needs mask & 3
    const LightTable table = t.read();
    assert(candidate_count(table, 8 | 1, ListKind::secondary) == 2);   // 0 and 2
    assert(candidate_count(table, 8 | 2, ListKind::secondary) == 2);   // 1 and 2
    assert(candidate_count(table, 8, ListKind::secondary) == 0);
    assert(candidate_count(table, 4 | 3, ListKind::secondary) == 0);   // no bit 8
    assert(candidate_count(table, 8 | 3, ListKind::main) == 0);        // no bit 4
    assert(candidate_count(table, 4 | 3, ListKind::main) == 2);        // 0x0B never on main
}

void test_never_selected() {
    const Row none{x, x, x, x, x, x, x, x};
    for (std::uint8_t flags : {1, 2, 3}) {
        assert(selected_per_mask(flags, ListKind::main, 2) == none);
        assert(selected_per_mask(flags, ListKind::secondary, 2) == none);
        assert(selected_per_mask(flags, ListKind::main, 1, 1) == none);
        assert(selected_per_mask(flags, ListKind::secondary, 0x80) == none);
        assert(selected_per_mask(flags, ListKind::main, 1, 0x80) == none);
    }
    // Type 0 is a candidate like type 1.
    TableBytes t;
    t.set(0, {.flags = 3, .type = 0, .falloff = 0.0f});
    assert(candidate_count(t.read(), 7, ListKind::main) == 1);
}

// --- score -------------------------------------------------------------------

void test_score() {
    TableBytes t;
    t.set(0, {.flags = 3, .type = 0, .position = {10, 0, 0}, .falloff = 0.005f});
    LightTable table = t.read();
    auto sel = select_lights(table, {0, 0, 0}, 5);
    assert(sel.count == 1 && sel.index[0] == 0);
    assert(near(sel.score[0], 0.5f));
    assert(near(score(table.records[0], {0, 0, 0}), 0.5f));

    // Distance from the query, not the origin: delta (6, -8, 0) is 10 long.
    sel = select_lights(table, {4, 8, 0}, 5);
    assert(sel.count == 1 && near(sel.score[0], 0.5f));

    TableBytes far;
    far.set(0, {.flags = 3, .type = 0, .position = {20, 0, 0}, .falloff = 0.005f});
    table = far.read();
    assert(select_lights(table, {0, 0, 0}, 5).count == 0);
    assert(score(table.records[0], {0, 0, 0}) == 0.0f);
    assert(candidate_count(table, 5, ListKind::main) == 1);  // score not applied

    TableBytes edge;  // exactly 0 is dropped too
    edge.set(0, {.flags = 3, .type = 0, .position = {2, 0, 0}, .falloff = 0.25f});
    assert(select_lights(edge.read(), {0, 0, 0}, 5).count == 0);

    TableBytes directional;
    directional.set(0, {.flags = 3, .type = 1, .position = {1000, 0, 0}, .falloff = 1.0f});
    sel = select_lights(directional.read(), {0, 0, 0}, 5);
    assert(sel.count == 1 && sel.score[0] == 1.0f);
}

// --- ordering ----------------------------------------------------------------

void test_ordering() {
    // At distance 1 the score is 1 - falloff.
    TableBytes t;
    t.set(3, {.flags = 3, .type = 0, .position = {1, 0, 0}, .falloff = 0.4f});   // 0.6
    t.set(7, {.flags = 3, .type = 0, .position = {0, 1, 0}, .falloff = 0.1f});   // 0.9
    t.set(12, {.flags = 3, .type = 0, .position = {0, 0, 1}, .falloff = 0.5f});  // 0.5
    t.set(30, {.flags = 3, .type = 0, .position = {-1, 0, 0}, .falloff = 0.3f}); // 0.7
    t.set(41, {.flags = 3, .type = 0, .position = {0, -1, 0}, .falloff = 0.2f}); // 0.8
    const auto sel = select_lights(t.read(), {0, 0, 0}, 5);
    assert(sel.count == 4);
    assert((sel.index == std::array<int, 4>{7, 41, 30, 3}));
    assert(near(sel.score[0], 0.9f) && near(sel.score[1], 0.8f));
    assert(near(sel.score[2], 0.7f) && near(sel.score[3], 0.6f));
    for (int i = 0; i < 3; ++i) assert(sel.score[i] > sel.score[i + 1]);

    // Equal scores: higher index first; the lowest index is the one left out.
    TableBytes eq;
    for (std::size_t i : {2u, 9u, 17u, 25u, 47u}) eq.set(i, {.flags = 3, .type = 1});
    const auto tie = select_lights(eq.read(), {0, 0, 0}, 5);
    assert(tie.count == 4);
    assert((tie.index == std::array<int, 4>{47, 25, 17, 9}));

    // A type 0 at score 1 (falloff 0) ties with type 1 by index too.
    TableBytes mixed;
    mixed.set(5, {.flags = 3, .type = 1});
    mixed.set(6, {.flags = 3, .type = 0, .falloff = 0.0f, .rgb = {1, 0, 0}});
    const auto m = select_lights(mixed.read(), {0, 0, 0}, 5);
    assert(m.count == 2 && m.index[0] == 6 && m.index[1] == 5);
}

// --- matrices ----------------------------------------------------------------

void test_matrices() {
    TableBytes t;
    t.set(5, {.flags = 3, .type = 0, .position = {1, 2, 3}, .aux_w = 7.0f, .falloff = 0.01f,
              .rgb = {100, 200, 300}});
    t.set(9, {.flags = 3, .type = 1, .position = {4, 5, 6}, .aux_w = 8.0f, .falloff = 0.5f,
              .rgb = {10, 20, 30}});
    const auto sel = select_lights(t.read(), {1, 1, 1}, 5);
    assert(sel.count == 2);
    // Type 1 scores 1; type 0: delta (0, 1, 2), 1 - 5 * 0.01 = 0.95.
    assert(sel.index[0] == 9 && sel.index[1] == 5);
    assert(sel.index[2] == -1 && sel.index[3] == -1);
    assert(sel.score[0] == 1.0f && near(sel.score[1], 0.95f));
    assert(sel.score[2] == 0.0f && sel.score[3] == 0.0f);

    const std::array<float, 16> lc{10, 20, 30, 1,  100, 200, 300, 0.01f,  0, 0, 0, 0,  0, 0, 0, 0};
    const std::array<float, 16> lv{3, 4, 5, 0,  0, 1, 2, 7,  0, 0, 0, 0,  0, 0, 0, 0};
    assert(sel.lc == lc);
    assert(sel.lv == lv);

    const auto empty = select_lights(t.read(), {0, 0, 0}, 0);
    assert(empty.count == 0);
    assert((empty.index == std::array<int, 4>{-1, -1, -1, -1}));
    for (float v : empty.lc) assert(v == 0.0f);
    for (float v : empty.lv) assert(v == 0.0f);
}

// --- helpers -----------------------------------------------------------------

void test_describe() {
    TableBytes t(3);
    for (std::size_t i = 0; i < 6; ++i) t.set(i * 2, {.flags = 1, .type = 0, .falloff = 0.1f});
    for (std::size_t i = 0; i < 3; ++i) t.set(20 + i, {.flags = 3, .type = 1});
    t.set(4, {.flags = 1, .colour_modifier = 1, .type = 0});
    t.set(21, {.flags = 3, .colour_modifier = 9, .type = 1});
    const LightTable table = t.read();
    assert(describe(table) ==
           "LIG2 v3: 9 of 48 records active (type 0: 6, type 1: 3; 2 with a colour modifier)");
    assert(describe(TableBytes(0).read()) ==
           "LIG2 v0: 0 of 48 records active (type 0: 0, type 1: 0; 0 with a colour modifier)");
    // Records with a modifier are not candidates.
    assert(candidate_count(table, 7, ListKind::main) == 7);
}

}  // namespace

int main() {
    test_read_fields();
    test_read_size_and_magic();
    test_read_non_finite();
    test_find_light_table();
    test_eligibility_main();
    test_eligibility_secondary();
    test_never_selected();
    test_score();
    test_ordering();
    test_matrices();
    test_describe();
    return 0;
}
