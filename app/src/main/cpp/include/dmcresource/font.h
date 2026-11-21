#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

// FON: the game's bitmap fonts (font\euro28.fon, china20m.fon, china20z.fon,
// wksong_glim.fon). Reverse authority: dmc-rengine-cpp
// docs/research/dmc3-fon-bitmap-fonts-2026-10-10.md (formats/fon there).
//
// No tag, no header. +0x000: 256 bytes, the page number (1..P, 0 = none) for
// each UTF-16 high byte. +0x100: P pages of 256 u16 glyph numbers (1..G, 0 =
// none) by low byte. Then glyphs 1..G, equal size, rows of 1-bit pixels, most
// significant bit first. The cell is not stored: it is the narrowest
// whole-byte row at least as wide as the glyph is tall (32 x 28 for euro28,
// 24 x 20 for the 20-pixel fonts — the size in each name).
namespace dmcresource::font {

inline constexpr std::size_t kLeadTable = 0x100U;
inline constexpr std::size_t kPageSize = 0x200U;
/// The tallest sheet drawn: glyphs are legible only at their own size.
inline constexpr std::uint32_t kSheetMaxSide = 4096U;

struct Character final {
    std::uint16_t code{};   // UTF-16 code unit
    std::uint16_t number{}; // 1-based glyph number
};

struct Font final {
    std::uint32_t pages{};
    std::uint32_t glyph_count{};
    std::uint32_t glyph_bytes{};
    std::uint32_t row_bytes{};
    std::uint32_t width{};
    std::uint32_t height{};
    std::uint64_t glyph_offset{};
    std::vector<Character> characters; // code order
    std::vector<std::uint8_t> blocks;   // high bytes with a page
};

struct ReadResult final {
    std::optional<Font> font;
    std::string error;
    [[nodiscard]] bool ok() const noexcept { return font.has_value(); }
};

[[nodiscard]] ReadResult read(std::span<const std::uint8_t> bytes);
[[nodiscard]] bool looks_like_font(std::span<const std::uint8_t> bytes) noexcept;

/// One byte per pixel (0/1), width x height.
[[nodiscard]] std::vector<std::uint8_t> glyph_pixels(const Font& font, std::span<const std::uint8_t> bytes,
                                                     std::uint32_t number);

struct InkSpan final {
    std::uint32_t first{};
    std::uint32_t last{};
};
[[nodiscard]] std::optional<InkSpan> ink_span(const Font& font, std::span<const std::uint8_t> bytes,
                                              std::uint32_t number);

struct Sheet final {
    std::uint32_t width{};
    std::uint32_t height{};
    std::uint32_t columns{};
    std::uint32_t shown{};
    std::vector<std::uint8_t> rgba8;
};

/// Glyphs in code order, `columns` cells wide with 1 px gaps, dark ink on
/// light paper, within `max_pixels` and kSheetMaxSide tall.
[[nodiscard]] Sheet render_sheet(const Font& font, std::span<const std::uint8_t> bytes, std::uint32_t columns,
                                 std::uint64_t max_pixels);

/// The Unicode block a high byte opens, for listing coverage.
[[nodiscard]] const char* block_name(std::uint8_t high) noexcept;

}  // namespace dmcresource::font
