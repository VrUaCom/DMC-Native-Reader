#pragma once

#include <cstddef>
#include <string>
#include <string_view>

// Text that leaves the core for a shell must be UTF-8: Android's JNI stops
// the app on a string that is not, and file names, strings found in game
// files and cut names can hold any bytes.
namespace dmcresource::text {

// Bytes of the UTF-8 character that starts at `at`; 0 when the bytes there
// are not one (a stray continuation byte, a cut or overlong sequence).
[[nodiscard]] constexpr std::size_t utf8_character(std::string_view text, std::size_t at) noexcept {
    if (at >= text.size()) return 0U;
    const auto byte = [&](std::size_t i) { return static_cast<unsigned char>(text[i]); };
    const unsigned lead = byte(at);
    std::size_t length = 0U;
    if (lead < 0x80U) return 1U;
    if (lead >= 0xC2U && lead <= 0xDFU) length = 2U;
    else if (lead >= 0xE0U && lead <= 0xEFU) length = 3U;
    else if (lead >= 0xF0U && lead <= 0xF4U) length = 4U;
    else return 0U;
    if (at + length > text.size()) return 0U;
    for (std::size_t k = 1U; k < length; ++k) {
        if ((byte(at + k) & 0xC0U) != 0x80U) return 0U;
    }
    const unsigned second = byte(at + 1U);
    if (lead == 0xE0U && second < 0xA0U) return 0U;  // overlong
    if (lead == 0xEDU && second > 0x9FU) return 0U;  // a surrogate
    if (lead == 0xF0U && second < 0x90U) return 0U;  // overlong
    if (lead == 0xF4U && second > 0x8FU) return 0U;  // past U+10FFFF
    return length;
}

[[nodiscard]] constexpr bool is_utf8(std::string_view text) noexcept {
    for (std::size_t at = 0U; at < text.size();) {
        const std::size_t length = utf8_character(text, at);
        if (length == 0U) return false;
        at += length;
    }
    return true;
}

// `text` with every byte that is not part of a UTF-8 character as '?'.
[[nodiscard]] inline std::string as_utf8(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (std::size_t at = 0U; at < text.size();) {
        const std::size_t length = utf8_character(text, at);
        if (length == 0U) {
            out += '?';
            ++at;
        } else {
            out.append(text.substr(at, length));
            at += length;
        }
    }
    return out;
}

}  // namespace dmcresource::text
