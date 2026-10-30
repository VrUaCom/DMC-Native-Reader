// File health checks (file_health.h).
#include "dmcresource/file_health.h"

#include <algorithm>
#include <cctype>
#include <cstring>

namespace dmcresource::file_health {
namespace {

[[nodiscard]] std::uint32_t u32(std::span<const std::uint8_t> b, std::size_t at) noexcept {
    return static_cast<std::uint32_t>(b[at]) | static_cast<std::uint32_t>(b[at + 1U]) << 8U |
           static_cast<std::uint32_t>(b[at + 2U]) << 16U | static_cast<std::uint32_t>(b[at + 3U]) << 24U;
}

[[nodiscard]] bool starts(std::span<const std::uint8_t> b, std::string_view magic) noexcept {
    return b.size() >= magic.size() && std::memcmp(b.data(), magic.data(), magic.size()) == 0;
}

[[nodiscard]] std::string lower(std::string_view text) {
    std::string out{text};
    for (auto& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

[[nodiscard]] std::string extension(std::string_view name) {
    const auto dot = name.rfind('.');
    return dot == std::string_view::npos ? std::string{} : lower(name.substr(dot + 1U));
}

// The game's own file kinds: a picture or an archive of another program
// under one of these names is not a game file.
[[nodiscard]] bool game_extension(const std::string& ext) noexcept {
    static constexpr std::string_view kinds[] = {"pac", "mod", "scm", "ptx", "mot", "shw", "hit", "hits",
                                                 "efm", "clt", "tsc", "evt", "lig", "dca", "nbz", "afs"};
    return std::find(std::begin(kinds), std::end(kinds), ext) != std::end(kinds);
}

[[nodiscard]] std::string size_text(std::size_t size) {
    return std::to_string(size) + " bytes";
}

void foreign(Report& report, std::span<const std::uint8_t> b, const std::string& ext) {
    // The Reader's diagnostics log (an effects pick of v72-v134 wrote it over
    // the picked file).
    const std::string_view head{reinterpret_cast<const char*>(b.data()), std::min<std::size_t>(b.size(), 256U)};
    if (head.starts_with("[20") && head.find(" build=") != std::string_view::npos &&
        head.find(" route=") != std::string_view::npos) {
        report.findings.push_back({Level::problem, "The Reader's diagnostics log, not a game file",
                                   "The file holds the Reader's own diagnostics log. Versions 1.0.45-1.0.107 "
                                   "wrote it over a file picked for stage effects. Copy the original from the "
                                   "game again."});
        return;
    }
    if (!game_extension(ext)) return;
    struct Magic final {
        std::string_view bytes;
        const char* what;
    };
    static constexpr Magic magics[] = {
        {"PK\x03\x04", "a ZIP archive"}, {"Rar!", "a RAR archive"}, {"7z\xBC\xAF", "a 7-Zip archive"},
        {"\x89PNG", "a PNG picture"},     {"\xFF\xD8\xFF", "a JPEG picture"}, {"<!DOCTYPE", "a web page"},
        {"<html", "a web page"},          {"%PDF", "a PDF document"},
    };
    for (const auto& magic : magics) {
        if (!starts(b, magic.bytes)) continue;
        report.findings.push_back({Level::problem, std::string{"Holds "} + magic.what,
                                   "Named as a game file (." + ext + "), but the bytes are " + magic.what +
                                       ". The game cannot read it; the original file is missing."});
        return;
    }
}

void pac_structure(Report& report, std::span<const std::uint8_t> b) {
    if (!starts(b, std::string_view{"PAC\0", 4U}) || b.size() < 8U) return;
    const std::uint32_t count = u32(b, 4U);
    if (count == 0U) {
        report.findings.push_back({Level::notice, "An empty PAC", "The archive declares no slots."});
        return;
    }
    if (8U + std::size_t{count} * 4U > b.size()) {
        report.findings.push_back({Level::problem, "The PAC is cut short",
                                   "It declares " + std::to_string(count) + " slots, but the slot table alone "
                                   "needs " + size_text(8U + std::size_t{count} * 4U) + " and the file has " +
                                   size_text(b.size()) + "."});
        return;
    }
    std::size_t past = 0U;
    std::uint32_t first_past = 0U, first_offset = 0U;
    for (std::uint32_t slot = 0U; slot < count; ++slot) {
        const std::uint32_t offset = u32(b, 8U + std::size_t{slot} * 4U);
        if (offset > b.size()) {
            if (past == 0U) {
                first_past = slot;
                first_offset = offset;
            }
            ++past;
        }
    }
    if (past > 0U) {
        report.findings.push_back({Level::problem, "The PAC is cut short",
                                   std::to_string(past) + " of " + std::to_string(count) +
                                       " slots start past the end of the file (slot " + std::to_string(first_past) +
                                       " at " + std::to_string(first_offset) + ", the file has " +
                                       size_text(b.size()) + "). The copy was not finished."});
    }
}

void name_checks(Report& report, std::string_view name, std::span<const std::uint8_t> b, bool recognized) {
    const std::string low = lower(name);
    // A copy the system or a file manager made: the game opens files by
    // their exact names.
    for (const std::string_view mark : {" (", "copy", "копія", "kopie", " - "}) {
        if (low.find(mark) == std::string::npos) continue;
        report.findings.push_back({Level::notice, "The name is not the game's",
                                   "\"" + std::string{name} + "\" looks like a copy's name. The game looks the "
                                   "file up by its exact name, so under this one it would not be found."});
        break;
    }
    if (low.find(' ') != std::string::npos && report.findings.empty()) {
        report.findings.push_back({Level::notice, "A space in the name",
                                   "Game file names have no spaces; this one would not be found."});
    }
    const auto ext = extension(name);
    const auto dot = low.rfind('.');
    if (dot != std::string::npos && dot > 0U) {
        const auto stem = low.substr(0U, dot);
        const auto inner = stem.rfind('.');
        if (inner != std::string::npos && extension(stem) == ext) {
            report.findings.push_back({Level::notice, "A doubled extension",
                                       "The name ends in ." + ext + "." + ext + "."});
        }
    }
    if (ext == "pac" && recognized && report.worst() != Level::problem && b.size() >= 4U && !starts(b, std::string_view{"PAC\0", 4U}) &&
        !starts(b, "PNST")) {
        report.findings.push_back({Level::notice, "Not a PAC inside",
                                   "Named .pac, but the contents are another format (the Reader read them as "
                                   "such)."});
    }
}

}  // namespace

Level Report::worst() const noexcept {
    Level worst = Level::fine;
    for (const auto& finding : findings) worst = std::max(worst, finding.level);
    return worst;
}

Report check(std::string_view name, std::span<const std::uint8_t> bytes, bool recognized,
             std::span<const std::string> notes) {
    Report report;
    try {
        if (bytes.empty()) {
            report.findings.push_back({Level::problem, "An empty file", "The file has no bytes."});
            return report;
        }
        if (std::all_of(bytes.begin(), bytes.end(), [](std::uint8_t b) { return b == 0U; })) {
            report.findings.push_back({Level::problem, "Only zero bytes",
                                       "All " + size_text(bytes.size()) + " are zero: the contents were never "
                                       "written (an interrupted copy or download)."});
            return report;
        }
        const auto before = report.findings.size();
        foreign(report, bytes, extension(name));
        if (report.findings.size() == before) pac_structure(report, bytes);
        name_checks(report, name, bytes, recognized);
        if (!recognized && report.worst() != Level::problem) {
            report.findings.push_back({Level::notice, "No reader for these contents",
                                       "None of the Reader's formats took the file, so it shows as raw bytes. "
                                       "That says nothing about whether the game loads it."});
        }
        for (const auto& note : notes) {
            report.findings.push_back({Level::notice, "Read in a way the game's data is not specified", note});
        }
    } catch (...) {
    }
    return report;
}

std::string format(std::string_view name, const Report& report) {
    std::string out;
    try {
        out = "FILE HEALTH\n";
        out += std::string{name} + "\n\n";
        const auto worst = report.worst();
        if (worst == Level::fine) {
            out += "No problems found: the bytes and the name look like a game file.\n";
            return out;
        }
        std::size_t problems = 0U, notices = 0U;
        for (const auto& f : report.findings) (f.level == Level::problem ? problems : notices) += 1U;
        out += worst == Level::problem ? "The game would not load this file as it is.\n"
                                       : "The game file is readable; small mismatches are listed.\n";
        out += std::to_string(problems) + " problem(s), " + std::to_string(notices) + " notice(s)\n\n";
        for (const auto level : {Level::problem, Level::notice}) {
            for (const auto& f : report.findings) {
                if (f.level != level) continue;
                out += level == Level::problem ? "\xF0\x9F\x94\xB4 " : "\xF0\x9F\x9F\xA0 ";  // red / orange circle
                out += f.title + "\n   " + f.detail + "\n\n";
            }
        }
    } catch (...) {
    }
    return out;
}

}  // namespace dmcresource::file_health
