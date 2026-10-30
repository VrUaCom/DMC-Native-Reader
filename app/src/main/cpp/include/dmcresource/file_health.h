#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// File health: whether a file as it lies on disk is one the game would take,
// read from its bytes and its name. The Reader only looks; nothing is
// changed. Two levels:
// - problem: the game would not get past it (the file is empty, all zero,
//   cut short, or holds something else entirely, such as the Reader's own
//   diagnostics log written over it);
// - notice: small mismatches the Reader reads past (a copy suffix in the
//   name, an extension that does not fit the contents, parts read in a way
//   the game's data is not specified, no reader for the format).
namespace dmcresource::file_health {

enum class Level : std::uint8_t { fine = 0, notice = 1, problem = 2 };

struct Finding final {
    Level level{Level::notice};
    std::string title;
    std::string detail;
};

struct Report final {
    std::vector<Finding> findings;
    [[nodiscard]] Level worst() const noexcept;
};

// `recognized`: a reader of the Reader took the file (false: it opened as raw
// bytes). `notes`: the session's non-canonical notes, each a notice.
[[nodiscard]] Report check(std::string_view name, std::span<const std::uint8_t> bytes, bool recognized,
                           std::span<const std::string> notes = {});

// The report as the Info window shows it.
[[nodiscard]] std::string format(std::string_view name, const Report& report);

}  // namespace dmcresource::file_health
