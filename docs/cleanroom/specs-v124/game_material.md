# Spec: object material state (sampler, part alpha)

Write `app/src/main/cpp/include/dmcresource/game_material.h` and
`app/src/main/cpp/modules/game_material.cpp` (namespace
`dmcresource::game_material`, C++23, no third-party headers), and the test
`app/src/test/native/game_material_test.cpp` (plain `assert`, a `main` that
runs every check and returns 0).

Every model or stage object carries a 32-bit `source_flags` word, an 8-bit
`alpha_control` byte and, for each of its meshes, four 16-bit words
`min_u`, `max_u`, `min_v`, `max_v`. This module turns them into the state
the Reader draws with.

## 1. Texture filter

`bool nearest_filter(std::uint32_t source_flags)`: true when bit `0x4000` is
set (point sampling), otherwise false (linear sampling).

## 2. Texture addressing

A mesh's four words describe a legacy console "clamp" register:

- `std::optional<std::uint64_t> legacy_clamp_register(min_u, max_u, min_v, max_v)`:
  - when `min_u == 0` → `std::nullopt` (no register: the mesh keeps the
    default addressing);
  - otherwise the 64-bit value
    `(min_u << 4) | 0x0F | (max_u << 14) | (min_v << 24) | (max_v << 34)`,
    computed in 64 bits, with the words **not** masked first.
  - The low 2 bits of the value are the U mode, the next 2 bits the V mode.
    The constant `0x0F` therefore sets both modes to 3 ("region repeat").
- `enum class Address : std::uint8_t { wrap, clamp };`
- `Address address_for_mode(unsigned mode)`: modes 1 and 2 → `clamp`;
  0 and 3 (and anything else) → `wrap`.
- `struct Addressing { Address u{Address::wrap}; Address v{Address::wrap}; };`
- `Addressing addressing(min_u, max_u, min_v, max_v)`: no register → both
  `wrap`; otherwise U from bits 0..1 and V from bits 2..3 of the register,
  through `address_for_mode`. (On the target platform this always gives
  `wrap`. Keep the general rule.)

## 3. Part alpha

- `std::uint8_t effective_alpha_control(std::uint32_t source_flags, std::uint8_t alpha_control)`:
  when the low nibble `source_flags & 0xF` is non-zero, the result is
  `0x80`; otherwise it is `alpha_control` unchanged.
- `float part_alpha(std::uint8_t effective)`: above `0x80` → `1.0f`;
  otherwise `effective / 255.0f` (so `0x80` → `128/255`).
- `bool alpha_override(std::uint8_t effective)`: true above `0x80`.

## 4. The per-vertex material byte

The Reader carries one byte per vertex for its object (`Mesh::blend0`). Its
low nibble is already the object's blend mode (`source_flags & 0xF`: 0
opaque, 1 and 4 alpha, 2 additive, 3 subtractive). Extend it:

| Bits | Meaning |
| --- | --- |
| 0..3 | blend mode, as before |
| 4 | nearest filter (section 1) |
| 5 | alpha override: part alpha 1.0 (section 3) |
| 6..7 | zero |

- `std::uint8_t material_byte(std::uint32_t source_flags, std::uint8_t alpha_control)`:
  - blend nibble = `source_flags & 0xF`;
  - bit 4 = `nearest_filter`;
  - bit 5 = `alpha_override(effective_alpha_control(...))`.
- `std::uint8_t material_blend(std::uint8_t byte)` → `byte & 0xF`.
- `bool material_nearest(std::uint8_t byte)` → bit 4.
- `float material_part_alpha(std::uint8_t byte)` → `1.0f` with bit 5,
  otherwise `128.0f / 255.0f`. An effective control below 0x80 is not
  carried; it reads back as 128/255. Say so in the header comment.

All functions are `constexpr` or `noexcept` where they can be. They must
not throw. The header comments say in one line each what the rule is.
They do not name addresses or any other program.

## Tests (at least)

- `nearest_filter(0x4000)`, `!nearest_filter(0x3FFF)`.
- `legacy_clamp_register(0, 5, 6, 7)` is empty;
  `legacy_clamp_register(1, 2, 3, 4) == (1<<4 | 0xF | 2<<14 | 3<<24 | 4ull<<34)`;
  a word above 0x3FF is not masked (`min_u = 0x400` gives `0x400ull << 4 | ...`).
- `addressing` of any non-zero `min_u` is `wrap`/`wrap`; `address_for_mode`
  for 0, 1, 2, 3.
- `effective_alpha_control(0x2, 0xC1) == 0x80`; `(0x4000, 0xC1) == 0xC1`.
- `part_alpha(0x80) == 128/255`, `part_alpha(0xC5) == 1`,
  `part_alpha(0x40) == 64/255`.
- `material_byte` round trips: blend 2 stays 2; `0x4000` sets the nearest
  bit; control 0xC3 with blend 0 sets the override; the same with blend 2
  does not (the forced 0x80).
