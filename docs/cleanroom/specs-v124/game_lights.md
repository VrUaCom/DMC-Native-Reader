# Spec: game light records, top-four selection, light matrices

Write `app/src/main/cpp/include/dmcresource/game_lights.h` and
`app/src/main/cpp/modules/game_lights.cpp` (namespace
`dmcresource::game_lights`, C++23, no third-party headers), and the test
`app/src/test/native/game_lights_test.cpp` (plain `assert`, a `main` that
runs every check and returns 0). Floats are `float` (32-bit) throughout.

## 1. The light table file

A light table is little-endian bytes:

- `+0x00`: the 4 ASCII bytes `LIG2`.
- `+0x00 .. +0x1F`: header (32 bytes). Keep `u16` at `+0x04` as `version`
  and the six `u16` at `+0x06 .. +0x10` as `header_words` (their meaning is
  unknown; keep them for display only).
- `+0x20`: 48 records of `0x30` bytes each. The table is `0x920` bytes;
  accept longer input (ignore the rest), reject shorter input.

Record layout (offsets inside the record):

| Offset | Type | Name |
| --- | --- | --- |
| 0x00 | u8 | `flags` |
| 0x01 | u8 | `colour_modifier` |
| 0x02 | u8 | `type` |
| 0x10, 0x14, 0x18 | f32 ×3 | `position` (x, y, z) |
| 0x1C | f32 | `aux_w` |
| 0x20 | f32 | `falloff` |
| 0x24, 0x26, 0x28 | u16 ×3 | `rgb` |

A record is `active` when any of its 0x30 bytes is non-zero.

```cpp
struct LightRecord {
    std::uint8_t index{};          // 0..47
    bool active{};
    std::uint8_t flags{}, colour_modifier{}, type{};
    std::array<float, 3> position{};
    float aux_w{}, falloff{};
    std::array<std::uint16_t, 3> rgb{};
};
struct LightTable {
    std::uint16_t version{};
    std::array<std::uint16_t, 6> header_words{};
    std::array<LightRecord, 48> records{};
};
std::optional<LightTable> read_light_table(std::span<const std::uint8_t> bytes) noexcept;
```

Reject the input (return `std::nullopt`) when it is too short, the magic
differs, or any `position`, `aux_w` or `falloff` of an **active** record is
not finite. Also provide
`std::optional<LightTable> find_light_table(std::span<const std::uint8_t> bytes) noexcept`,
which returns the first offset (aligned to 4) where `read_light_table`
succeeds. It is used on a container file that embeds the table.

## 2. Which records may light a query

A selection takes a `mask` (bits) and a list kind (`main` or `secondary`).
A record is a candidate when all of these hold:

1. it is `active`;
2. `colour_modifier == 0` (records with a modifier are not handled here and
   are never selected);
3. `type` is 0 or 1;
4. the list's mask bit is set: `main` needs `mask & 4`, `secondary` needs
   `mask & 8`;
5. its low mode is allowed. Take `f = flags`. For the secondary list, first
   clear bit 3 (`f &= 0xF7`). Then `low = f & ~4u` (clear bit 2):
   - `low == 1` → needs `mask & 3`;
   - `low == 2` → needs `mask & 2`;
   - `low == 3` → needs `mask & 1`;
   - any other `low` → never.

## 3. Score

For a candidate and a query point `q`:

- `delta = position − q` (per component, in `float`);
- type 0: `score = max(0, 1 − dot(delta, delta) * falloff)`;
- type 1: `score = 1`.

A candidate whose score is `<= 0` is dropped.

## 4. Top four

Sort the remaining candidates by score, highest first. Among equal scores,
the record with the **higher index comes first**. Keep at most four.

## 5. Light matrices

```cpp
struct SelectedLights {
    int count{};                                  // 0..4
    std::array<int, 4> index{-1, -1, -1, -1};     // record index per slot
    std::array<float, 4> score{};
    std::array<float, 16> lc{};                   // 4 rows of 4: colour
    std::array<float, 16> lv{};                   // 4 rows of 4: vector
};
SelectedLights select_lights(const LightTable& table, std::array<float, 3> query,
                             unsigned mask, ListKind list = ListKind::main) noexcept;
```

`enum class ListKind : std::uint8_t { main, secondary };`

For each kept light in slot `i` (rows `4i .. 4i+3`):

- `lc` row = `(rgb[0], rgb[1], rgb[2], w)` as floats, with `w = falloff`
  for type 0 and `1` for type 1;
- `lv` row = `(delta.x, delta.y, delta.z, w)` with `w = aux_w` for type 0
  and `0` for type 1.

Unused slots stay zero; their `index` stays −1.

## 6. Helpers for a renderer

- `std::size_t candidate_count(const LightTable&, unsigned mask, ListKind)`
  counts the records that pass section 2 (score not applied).
- `std::string describe(const LightTable&)`: one line, e.g.
  `"LIG2 v3: 9 of 48 records active (type 0: 6, type 1: 3; 2 with a colour modifier)"`.

No function throws. Header comments state each rule in one line. They do
not name addresses or any other program.

## Tests (at least)

Build tables in memory (a helper writes the bytes):

- Reading: magic and size checks; a non-finite position in an active
  record rejects the table; a non-finite value in an inactive (all zero)
  record cannot occur. `find_light_table` finds a table placed at offset
  0x40 of a larger buffer.
- Eligibility: each `low` value with masks 1, 2, 3, 4, 5, 6, 7, 11; flag
  bit 2 is ignored (flags 7 behaves like 3); a secondary record with flags
  `0x0B` behaves like 3; main needs mask bit 4 and secondary bit 8; type 2
  and a colour modifier are never selected.
- Score: type 0 at distance 10 with falloff 0.005 → 0.5; at distance 20 →
  dropped; type 1 → 1.
- Ordering: five candidates; the top four are in score order; equal scores
  put the higher index first.
- Matrices: the `lc` and `lv` rows of a type 0 and a type 1 light, exactly
  as in section 5; unused rows are zero.
