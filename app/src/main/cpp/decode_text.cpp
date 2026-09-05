#include "dmcresource/text_decode.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <new>
#include <string>
#include <string_view>
#include <vector>

namespace dmcresource {
namespace {

// A stage text or manifest is configuration, not payload; anything larger than
// this is not one, and the viewer must not try to lay it out.
constexpr std::size_t kMaxTextBytes = 8u * 1024u * 1024u;

// Stage-set values recognised by the authority's lexer.
constexpr std::array<std::string_view, 7> kStageSetValues{
    "DUMMY", "STAY", "BREAK", "ORBREAK", "SEAL", "SWITCH", "YURE",
};

[[nodiscard]] bool identifier_start(char value) noexcept {
    const auto byte = static_cast<unsigned char>(value);
    return std::isalpha(byte) != 0 || value == '_';
}

[[nodiscard]] bool identifier_continue(char value) noexcept {
    const auto byte = static_cast<unsigned char>(value);
    return std::isalnum(byte) != 0 || value == '_';
}

[[nodiscard]] std::string uppercase(std::string_view value) {
    std::string out(value);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
        return static_cast<char>(std::toupper(c));
    });
    return out;
}

[[nodiscard]] bool contains_nul(const std::uint8_t* bytes, std::size_t size) noexcept {
    for (std::size_t i = 0; i < size; ++i) {
        if (bytes[i] == 0u) return true;
    }
    return false;
}

void append_note(std::string* notes, const char* note) {
    if (!notes->empty()) notes->append("; ");
    notes->append(note);
}

// Shared entry gate for both text families.
[[nodiscard]] bool text_payload_ok(const std::uint8_t* bytes, std::size_t size,
                                   Format format, DecodeResult* rejection) {
    if (size == 0) {
        *rejection = {DecodeStatus::InvalidData, format, {},
                      "resource is empty", {}, {}};
        return false;
    }
    if (size > kMaxTextBytes) {
        *rejection = {DecodeStatus::TooLarge, format, {},
                      "text resource exceeds decoder cap", {}, {}};
        return false;
    }
    if (bytes == nullptr) {
        *rejection = {DecodeStatus::InvalidData, format, {},
                      "null byte span", {}, {}};
        return false;
    }
    if (contains_nul(bytes, size)) {
        *rejection = {DecodeStatus::InvalidData, format, {},
                      "resource contains NUL bytes and is not text", {}, {}};
        return false;
    }
    return true;
}

struct StageTxtCounts {
    std::uint32_t directive_set{};
    std::uint32_t door{};
    std::uint32_t box_in{};
    std::uint32_t next_room{};
    std::uint32_t stage_set_value{};
    std::uint32_t identifier{};
    std::uint32_t number{};
    std::uint32_t string_literal{};
    std::uint32_t symbol{};

    [[nodiscard]] std::uint32_t total() const noexcept {
        return directive_set + door + box_in + next_room + stage_set_value +
               identifier + number + string_literal + symbol;
    }
};

void classify_word(const std::string& word, StageTxtCounts* counts) {
    const auto upper = uppercase(word);
    if (upper == "#SET") {
        ++counts->directive_set;
        return;
    }
    if (upper == "DOOR") {
        ++counts->door;
        return;
    }
    if (upper == "BOXIN") {
        ++counts->box_in;
        return;
    }
    if (upper == "NEXTROOM") {
        ++counts->next_room;
        return;
    }
    if (std::find(kStageSetValues.begin(), kStageSetValues.end(), upper) !=
        kStageSetValues.end()) {
        ++counts->stage_set_value;
        return;
    }
    ++counts->identifier;
}

}  // namespace

DecodeResult decode_stage_txt(const std::uint8_t* bytes, std::size_t size) noexcept {
    try {
        DecodeResult rejection;
        if (!text_payload_ok(bytes, size, Format::StageTxt, &rejection)) {
            return rejection;
        }

        const char* text = reinterpret_cast<const char*>(bytes);
        StageTxtCounts counts;
        std::string notes;

        std::size_t offset = 0;
        while (offset < size) {
            const char current = text[offset];
            if (std::isspace(static_cast<unsigned char>(current)) != 0) {
                ++offset;
                continue;
            }

            if (current == '/' && offset + 1u < size) {
                const char next = text[offset + 1u];
                if (next == '/') {
                    while (offset < size && text[offset] != '\n') ++offset;
                    continue;
                }
                if (next == '*') {
                    offset += 2u;
                    bool closed = false;
                    while (offset < size) {
                        if (text[offset] == '*' && offset + 1u < size &&
                            text[offset + 1u] == '/') {
                            offset += 2u;
                            closed = true;
                            break;
                        }
                        ++offset;
                    }
                    if (!closed) {
                        append_note(&notes, "block comment is unterminated");
                        break;
                    }
                    continue;
                }
            }

            if (current == '"') {
                ++offset;
                bool closed = false;
                bool crossed_line = false;
                while (offset < size) {
                    const char item = text[offset];
                    if (item == '"') {
                        ++offset;
                        closed = true;
                        break;
                    }
                    if (item == '\\' && offset + 1u < size) {
                        offset += 2u;
                        continue;
                    }
                    if (item == '\n' || item == '\r') {
                        crossed_line = true;
                        break;
                    }
                    ++offset;
                }
                if (crossed_line) {
                    append_note(&notes, "a quoted string crosses a line boundary");
                    break;
                }
                if (!closed) {
                    append_note(&notes, "a quoted string is unterminated");
                    break;
                }
                ++counts.string_literal;
                continue;
            }

            if (current == '#' || identifier_start(current)) {
                const std::size_t start = offset;
                ++offset;
                while (offset < size && identifier_continue(text[offset])) ++offset;
                classify_word(std::string(text + start, offset - start), &counts);
                continue;
            }

            const bool next_is_digit =
                offset + 1u < size &&
                std::isdigit(static_cast<unsigned char>(text[offset + 1u])) != 0;
            const bool numeric_start =
                std::isdigit(static_cast<unsigned char>(current)) != 0 ||
                ((current == '+' || current == '-' || current == '.') && next_is_digit);
            if (numeric_start) {
                bool exponent_seen = false;
                bool decimal_seen = false;
                ++offset;
                while (offset < size) {
                    const char item = text[offset];
                    if (std::isdigit(static_cast<unsigned char>(item)) != 0) {
                        ++offset;
                        continue;
                    }
                    if (item == '.' && !decimal_seen && !exponent_seen) {
                        decimal_seen = true;
                        ++offset;
                        continue;
                    }
                    if ((item == 'e' || item == 'E') && !exponent_seen) {
                        exponent_seen = true;
                        ++offset;
                        if (offset < size && (text[offset] == '+' || text[offset] == '-')) {
                            ++offset;
                        }
                        continue;
                    }
                    break;
                }
                ++counts.number;
                continue;
            }

            ++offset;
            ++counts.symbol;
        }

        if (counts.total() == 0) {
            return {DecodeStatus::InvalidData, Format::StageTxt, {},
                    "text contains no stage tokens", {}, {}};
        }

        std::string info = "tokens=" + std::to_string(counts.total()) +
                           " set=" + std::to_string(counts.directive_set) +
                           " door=" + std::to_string(counts.door) +
                           " boxin=" + std::to_string(counts.box_in) +
                           " nextroom=" + std::to_string(counts.next_room) +
                           " stageSet=" + std::to_string(counts.stage_set_value) +
                           " ident=" + std::to_string(counts.identifier) +
                           " num=" + std::to_string(counts.number) +
                           " str=" + std::to_string(counts.string_literal);
        if (!notes.empty()) info += " [" + notes + "]";

        return {DecodeStatus::Ok, Format::StageTxt, {},
                "DMC3 stage text lex", std::move(info),
                std::string(text, size)};
    } catch (const std::bad_alloc&) {
        return {DecodeStatus::TooLarge, Format::StageTxt, {},
                "decoder allocation failed", {}, {}};
    } catch (...) {
        return {DecodeStatus::InvalidData, Format::StageTxt, {},
                "decoder rejected malformed input", {}, {}};
    }
}

DecodeResult decode_index(const std::uint8_t* bytes, std::size_t size) noexcept {
    try {
        DecodeResult rejection;
        if (!text_payload_ok(bytes, size, Format::Index, &rejection)) {
            return rejection;
        }

        const char* text = reinterpret_cast<const char*>(bytes);
        std::uint32_t entries = 0;
        std::uint32_t dummies = 0;
        std::uint32_t comments = 0;
        std::string magic_directive;

        std::size_t offset = 0;
        bool first_content_line = true;
        while (offset < size) {
            std::size_t end = offset;
            while (end < size && text[end] != '\n') ++end;
            std::size_t line_end = end;
            if (line_end > offset && text[line_end - 1u] == '\r') --line_end;

            const std::string_view line(text + offset, line_end - offset);
            offset = end < size ? end + 1u : size;

            if (line.empty()) continue;
            // '/' opens a comment line in the list grammar.
            if (line.front() == '/') {
                ++comments;
                continue;
            }
            // A leading '#' marks an explicit magic directive; the real corpus
            // also opens with the bare literal line `PNST`. Either way this is
            // textual metadata, never binary container magic.
            if (first_content_line &&
                (line.front() == '#' || uppercase(line) == "PNST" ||
                 uppercase(line) == "PAC")) {
                magic_directive = std::string(line);
                first_content_line = false;
                continue;
            }
            first_content_line = false;

            ++entries;
            if (uppercase(line) == "DUMMY") ++dummies;
        }

        if (entries == 0 && magic_directive.empty()) {
            return {DecodeStatus::InvalidData, Format::Index, {},
                    "manifest declares no entries", {}, {}};
        }

        std::string info = "entries=" + std::to_string(entries) +
                           " dummy=" + std::to_string(dummies) +
                           " comments=" + std::to_string(comments) +
                           " directive=" +
                           (magic_directive.empty() ? std::string("none") : magic_directive);
        // The authority is explicit that this manifest is extraction/naming
        // metadata, not a runtime lookup or format authority.
        info += " [metadata manifest, not a container]";

        return {DecodeStatus::Ok, Format::Index, {},
                "DMC3 index manifest", std::move(info),
                std::string(text, size)};
    } catch (const std::bad_alloc&) {
        return {DecodeStatus::TooLarge, Format::Index, {},
                "decoder allocation failed", {}, {}};
    } catch (...) {
        return {DecodeStatus::InvalidData, Format::Index, {},
                "decoder rejected malformed input", {}, {}};
    }
}

}  // namespace dmcresource
