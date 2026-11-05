#include "dmcresource/font.h"

#include <algorithm>
#include <cstring>
#include <string>

namespace dmcresource::font {
namespace {

[[nodiscard]] std::uint16_t u16(std::span<const std::uint8_t> bytes, std::size_t at) noexcept {
    std::uint16_t value = 0U;
    std::memcpy(&value, bytes.data() + at, sizeof value);
    return value;
}

[[nodiscard]] ReadResult refuse(std::string error) {
    ReadResult result;
    result.error = std::move(error);
    return result;
}

}  // namespace

ReadResult read(std::span<const std::uint8_t> bytes) {
    if (bytes.size() < kLeadTable + kPageSize) return refuse("shorter than the lead table and one page");
    Font font;
    std::vector<bool> seen(257U, false);
    std::uint32_t pages = 0U;
    for (std::size_t high = 0U; high < kLeadTable; ++high) {
        const std::uint32_t page = bytes[high];
        if (page == 0U) continue;
        if (seen[page]) return refuse("two blocks name the same page");
        seen[page] = true;
        pages = std::max(pages, page);
        font.blocks.push_back(static_cast<std::uint8_t>(high));
    }
    if (pages == 0U || font.blocks.size() != pages) return refuse("the lead table does not number its pages 1..P");
    const auto glyph_offset = kLeadTable + std::size_t{pages} * kPageSize;
    if (bytes.size() <= glyph_offset) return refuse("the pages run past the end of the file");
    for (const auto high : font.blocks) {
        const auto base = kLeadTable + std::size_t{bytes[high] - 1U} * kPageSize;
        for (std::uint32_t low = 0U; low < 256U; ++low) {
            const auto number = u16(bytes, base + low * 2U);
            if (number != 0U) {
                font.characters.push_back(
                    {static_cast<std::uint16_t>((std::uint32_t{high} << 8U) | low), number});
            }
        }
    }
    std::uint32_t glyphs = 0U;
    for (const auto& c : font.characters) glyphs = std::max<std::uint32_t>(glyphs, c.number);
    std::vector<bool> used(glyphs + 1U, false);
    for (const auto& c : font.characters) {
        if (used[c.number]) return refuse("two characters share a glyph");
        used[c.number] = true;
    }
    if (glyphs == 0U || font.characters.size() != glyphs) return refuse("the pages do not number glyphs 1..G");
    const auto data = bytes.size() - glyph_offset;
    if (data % glyphs != 0U) return refuse("the glyph bytes do not divide into the glyph count");
    const auto glyph_bytes = static_cast<std::uint32_t>(data / glyphs);
    std::uint32_t row_bytes = 0U;
    for (std::uint32_t candidate = 1U; candidate <= 16U; ++candidate) {
        if (glyph_bytes % candidate == 0U && candidate * 8U >= glyph_bytes / candidate) {
            row_bytes = candidate;
            break;
        }
    }
    if (row_bytes == 0U) return refuse("a glyph's size has no cell shape");
    font.pages = pages;
    font.glyph_count = glyphs;
    font.glyph_bytes = glyph_bytes;
    font.row_bytes = row_bytes;
    font.width = row_bytes * 8U;
    font.height = glyph_bytes / row_bytes;
    font.glyph_offset = glyph_offset;
    ReadResult result;
    result.font = std::move(font);
    return result;
}

bool looks_like_font(std::span<const std::uint8_t> bytes) noexcept {
    try {
        return read(bytes).ok();
    } catch (...) {
        return false;
    }
}

std::vector<std::uint8_t> glyph_pixels(const Font& font, std::span<const std::uint8_t> bytes,
                                       std::uint32_t number) {
    std::vector<std::uint8_t> pixels(std::size_t{font.width} * font.height, 0U);
    if (number == 0U || number > font.glyph_count) return pixels;
    const auto base = font.glyph_offset + std::uint64_t{number - 1U} * font.glyph_bytes;
    if (base + font.glyph_bytes > bytes.size()) return pixels;
    for (std::uint32_t y = 0U; y < font.height; ++y) {
        for (std::uint32_t x = 0U; x < font.width; ++x) {
            const std::uint32_t byte = bytes[base + y * font.row_bytes + x / 8U];
            pixels[std::size_t{y} * font.width + x] = static_cast<std::uint8_t>((byte >> (7U - x % 8U)) & 1U);
        }
    }
    return pixels;
}

std::optional<InkSpan> ink_span(const Font& font, std::span<const std::uint8_t> bytes, std::uint32_t number) {
    const auto pixels = glyph_pixels(font, bytes, number);
    std::optional<InkSpan> span;
    for (std::uint32_t x = 0U; x < font.width; ++x) {
        for (std::uint32_t y = 0U; y < font.height; ++y) {
            if (pixels[std::size_t{y} * font.width + x] == 0U) continue;
            if (!span) span = InkSpan{x, x};
            span->last = x;
            break;
        }
    }
    return span;
}

Sheet render_sheet(const Font& font, std::span<const std::uint8_t> bytes, std::uint32_t columns,
                   std::uint64_t max_pixels) {
    Sheet sheet;
    columns = std::max<std::uint32_t>(1U, std::min<std::uint32_t>(columns, font.glyph_count));
    const auto cell_w = font.width + 1U;
    const auto cell_h = font.height + 1U;
    sheet.columns = columns;
    sheet.width = columns * cell_w + 1U;
    std::uint32_t rows = (font.glyph_count + columns - 1U) / columns;
    while (rows > 0U &&
           (std::uint64_t{sheet.width} * (rows * cell_h + 1U) > max_pixels || rows * cell_h + 1U > kSheetMaxSide)) {
        --rows;
    }
    if (rows == 0U) return sheet;
    sheet.height = rows * cell_h + 1U;
    sheet.shown = std::min<std::uint32_t>(font.glyph_count, rows * columns);
    sheet.rgba8.assign(std::size_t{sheet.width} * sheet.height * 4U, 0U);
    for (std::size_t i = 0U; i < sheet.rgba8.size(); i += 4U) {
        sheet.rgba8[i] = 0xD8U;
        sheet.rgba8[i + 1U] = 0xDCU;
        sheet.rgba8[i + 2U] = 0xE2U;
        sheet.rgba8[i + 3U] = 0xFFU;
    }
    for (std::uint32_t index = 0U; index < sheet.shown; ++index) {
        const auto pixels = glyph_pixels(font, bytes, font.characters[index].number);
        const auto left = 1U + (index % columns) * cell_w;
        const auto top = 1U + (index / columns) * cell_h;
        for (std::uint32_t y = 0U; y < font.height; ++y) {
            for (std::uint32_t x = 0U; x < font.width; ++x) {
                const auto at = (std::size_t{top + y} * sheet.width + left + x) * 4U;
                const bool ink = pixels[std::size_t{y} * font.width + x] != 0U;
                sheet.rgba8[at] = ink ? 0x10U : 0xFFU;
                sheet.rgba8[at + 1U] = ink ? 0x10U : 0xFFU;
                sheet.rgba8[at + 2U] = ink ? 0x18U : 0xFFU;
            }
        }
    }
    return sheet;
}

const char* block_name(std::uint8_t high) noexcept {
    if (high == 0x00U) return "Basic Latin and Latin-1";
    if (high == 0x01U) return "Latin Extended";
    if (high == 0x03U) return "Greek";
    if (high == 0x04U) return "Cyrillic";
    if (high == 0x20U) return "General Punctuation";
    if (high == 0x21U) return "Letterlike Symbols and Arrows";
    if (high == 0x25U) return "Box Drawing and Geometric Shapes";
    if (high == 0x26U) return "Miscellaneous Symbols";
    if (high == 0x30U) return "CJK Symbols, Hiragana and Katakana";
    if (high >= 0x4EU && high <= 0x9FU) return "CJK Unified Ideographs";
    if (high >= 0xACU && high <= 0xD7U) return "Hangul Syllables";
    if (high == 0xFFU) return "Halfwidth and Fullwidth Forms";
    return "Other";
}

}  // namespace dmcresource::font
