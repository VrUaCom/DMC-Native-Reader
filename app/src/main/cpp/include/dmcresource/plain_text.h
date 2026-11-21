#pragma once

// Plain text files inside the game's archives that no format module claims:
// light settings ("Force 1400.0 / Range / Rgb"), notes, small parameter
// lists. The Reader shows them as text instead of an unknown binary. A file
// is text when, past a UTF-8 BOM and before trailing zero padding, it holds
// no control bytes other than tab, line feed and carriage return and is
// mostly printable ASCII (Shift-JIS / UTF-8 / Windows-1252 bytes allowed).

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace dmcresource::plain_text {

[[nodiscard]] bool looks_like_text(std::span<const std::uint8_t> bytes) noexcept;

// The text's lines (BOM, trailing padding and CR removed), at most `limit`.
[[nodiscard]] std::vector<std::string> lines(std::span<const std::uint8_t> bytes, std::size_t limit);

}  // namespace dmcresource::plain_text
