# Spec: game light records, top-four selection, light matrices

Revision 3: section 1 gains three named header fields, and section 7
is new (the model's light rig). Keep every earlier rule.

Revision 2: sections 2 and 4 changed (flag bit 2 with mask bit 0x10; the
selection is an insertion into four slots in list order, not a sort), and
the manager's single light was added (section 4b). Update the module and
its tests to this revision; keep every rule not changed here.

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

Named header fields (revision 3). Keep `version` and `header_words` as
they are, and add:

| Field | Source | Meaning |
| --- | --- | --- |
| `static_light_count` | `+0x04` read as a **signed** 8-bit value (`std::int8_t`) | how many lights a model keeps from the table |
| `ambient` | the u16 at `+0x0C`, `+0x0E`, `+0x10`, each converted to `float` (220 → 220.0f, no scaling) | the ambient colour |
| `ambient_mask2` | the u16 at `+0x06`, `+0x08`, `+0x0A`, converted the same way | the ambient used when the mask is exactly 2 |

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
5. its flags allow it. Take `f = flags`. For the secondary list, first
   clear bit 3 (`f &= 0xF7`). The record passes when **either** of these
   holds:
   - **(a)** `f & 4` and `mask & 0x10`;
   - **(b)** its low mode allows it, with `low = f & ~4u` (clear bit 2):
     - `low == 1` → needs `mask & 3`;
     - `low == 2` → needs `mask & 2`;
     - `low == 3` → needs `mask & 1`;
     - any other `low` → (b) does not hold.

   Rules 1–3 are the Reader's: records the module cannot score are left
   out. Rules 4–5 are the game's.

## 3. Score

For a candidate and a query point `q`:

- `delta = position − q` (per component, in `float`);
- type 0: `score = max(0, 1 − dot(delta, delta) * falloff)`;
- type 1: `score = 1`.

A candidate whose score is `<= 0` is dropped.

## 4. Top four: insertion into four slots

There are four slots, in order 0 (best) to 3. Each starts empty with
score 0. The candidates are visited in **list order**, which for a light
table is record index 0, 1, ..., 47. For each candidate with score `s`:

1. If `s` is **not strictly greater** than slot 3's score, skip it.
2. Otherwise put it in slot 3, replacing what was there.
3. Then, from slot 3 upwards: while the slot above has a score `<=` the
   moved light's score, swap them.

Consequences, which the tests must check separately:
- A candidate with score `<= 0` never enters, because empty slots score 0.
- Among lights already kept, a **later** one with an equal score moves
  **above** an earlier one.
- With more than four equal scores, the **first four** visited are kept and
  later ones are skipped. For five equal scores at indices 0..4, the result
  is indices 3, 2, 1, 0.

`count` is the number of slots holding a light.

## 4b. The manager's single light

`select_lights` takes an optional extra record (`const LightRecord* single`,
default null). When it is given and `mask & 2`, after both of the above:

- score it as in section 3, with no eligibility test;
- move slots 0..2 down to 1..3 (the light in slot 3 is dropped);
- put it in slot 0, whatever its score.

If it then holds a light, it counts in `count`.

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
                             unsigned mask, ListKind list = ListKind::main,
                             const LightRecord* single = nullptr) noexcept;
```

`enum class ListKind : std::uint8_t { main, secondary };`

For each slot `i` holding a light (rows `4i .. 4i+3`), in slot order:

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

## 7. The model's light rig

```cpp
struct ModelRig {
    int count{};                                         // 0..3 lights
    std::array<std::array<float, 3>, 3> direction{};     // unit vectors
    std::array<std::array<float, 3>, 3> colour{};        // 0..255 units (128 = 1)
    std::array<float, 3> ambient{};                      // 0..255 units
};
ModelRig model_rig(const LightTable& table, const SelectedLights& lights,
                   unsigned mask, int limit = 3) noexcept;
```

- **Lights used.** The first `n = min(lights.count, 3, max(limit, 0))`
  slots of `lights`, in slot order. Slot 3 is never used. `count = n`.
- **For slot `i < n`,** with `lv` = row `i` of `lights.lv` and `lc` = row
  `i` of `lights.lc`:
  - attenuation `a`:
    - if `lv[3] != 0`: `a = max(0, 1 − (lv[0]² + lv[1]² + lv[2]²) · lv[3])`;
    - otherwise `a = 1`;
  - `colour[i] = (lc[0] · a, lc[1] · a, lc[2] · a)`. `lc[3]` is not used,
    and the score never scales the colour;
  - `direction[i] = (lv[0], lv[1], lv[2])` divided by its length. A zero
    vector stays zero.
- **Ambient.** `table.ambient_mask2` when `mask == 2` exactly, otherwise
  `table.ambient`.
- **Unused entries** stay zero.

## Tests (at least)

Build tables in memory (a helper writes the bytes):

- Reading: magic and size checks; a non-finite position in an active
  record rejects the table; a non-finite value in an inactive (all zero)
  record cannot occur. `find_light_table` finds a table placed at offset
  0x40 of a larger buffer.
- Eligibility: each `low` value with masks 1, 2, 3, 4, 5, 6, 7, 11, and the
  same masks with 0x10 added.
  - Flags 4 (low 0) pass only with mask bit 0x10.
  - Flags 7 pass where 3 passes, and also with 0x10.
  - A secondary record with flags `0x0B` behaves like 3, and `0x08` never
    passes (its bit 3 is cleared, and bit 2 is not set).
  - Main needs mask bit 4 and secondary bit 8.
  - Type 2 and a colour modifier are never selected.
- Score: type 0 at distance 10 with falloff 0.005 → 0.5; at distance 20 →
  dropped; type 1 → 1.
- Ordering:
  - five candidates with distinct scores come out in score order;
  - two equal scores put the later index first;
  - five equal scores keep indices 3, 2, 1, 0 (index 4 is skipped);
  - a candidate equal to the fourth score is skipped, one above it enters.
- Single light:
  - with mask & 2 it is slot 0 even with a lower score, and the old slot 3
    is dropped;
  - without mask & 2, or when null, nothing changes.
- Matrices: the `lc` and `lv` rows of a type 0 and a type 1 light, exactly
  as in section 5; unused rows are zero.
- Header fields (revision 3):
  - `static_light_count` of byte 0x03 is 3, and of 0xFF is −1;
  - `ambient` and `ambient_mask2` read the right words as floats (for
    example 220, 210, 200 and 32, 32, 32).
- Model rig (revision 3):
  - `count` is limited by `lights.count`, by 3 and by `limit` (a limit of
    0 or below gives 0);
  - the attenuation:
    - `lv[3] = 0` keeps the full colour;
    - `lv[3] = 0.005` at distance 10 halves it;
    - at distance 20 it gives 0 (clamped, not negative);
  - the colour ignores the score;
  - directions are unit length, and a zero `lv` gives a zero direction;
  - the ambient is `ambient` for masks 7 and 0, and `ambient_mask2` for 2.
