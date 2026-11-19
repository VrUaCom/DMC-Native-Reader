#include "dmcresource/plain_text.h"

namespace dmcresource::plain_text {
namespace {

std::span<const std::uint8_t> body(std::span<const std::uint8_t> bytes) noexcept {
    if (bytes.size() >= 3U && bytes[0] == 0xEFU && bytes[1] == 0xBBU && bytes[2] == 0xBFU) bytes = bytes.subspan(3U);
    std::size_t end = bytes.size();
    while (end > 0U && bytes[end - 1U] == 0U) --end;  // sector padding
    return bytes.first(end);
}

}  // namespace

bool looks_like_text(std::span<const std::uint8_t> bytes) noexcept {
    const auto text = body(bytes);
    if (text.size() < 4U || text.size() > 4U * 1024U * 1024U) return false;
    std::size_t printable = 0U;
    std::size_t letters = 0U;
    for (const auto b : text) {
        if (b == '\t' || b == '\n' || b == '\r') continue;
        if (b < 0x20U || b == 0x7FU) return false;  // a control byte: binary
        if (b < 0x7FU) {
            ++printable;
            if ((b >= 'A' && b <= 'Z') || (b >= 'a' && b <= 'z') || (b >= '0' && b <= '9')) ++letters;
        }
    }
    // Mostly ASCII, with some letters or digits (not a run of punctuation).
    return printable * 10U >= text.size() * 7U && letters >= 2U;
}

std::vector<std::string> lines(std::span<const std::uint8_t> bytes, std::size_t limit) {
    const auto text = body(bytes);
    std::vector<std::string> out;
    std::string line;
    for (const auto b : text) {
        if (b == '\n') {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (out.size() >= limit) return out;
            out.push_back(std::move(line));
            line.clear();
            continue;
        }
        line.push_back(static_cast<char>(b));
    }
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (!line.empty() && out.size() < limit) out.push_back(std::move(line));
    return out;
}

}  // namespace dmcresource::plain_text
