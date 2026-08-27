#include "dmcresource/dmc_resource.h"

#include <algorithm>
#include <cctype>
#include <string>

namespace dmcresource {
namespace {

std::string lower_extension(std::string_view filename) {
    const auto dot = filename.find_last_of('.');
    if (dot == std::string_view::npos || dot + 1 >= filename.size()) return {};
    std::string out(filename.substr(dot + 1));
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return out;
}

bool has_magic(const std::uint8_t* p, std::size_t n,
               char a, char b, char c, char d) noexcept {
    return p != nullptr && n >= 4 &&
           p[0] == static_cast<std::uint8_t>(a) &&
           p[1] == static_cast<std::uint8_t>(b) &&
           p[2] == static_cast<std::uint8_t>(c) &&
           p[3] == static_cast<std::uint8_t>(d);
}

}  // namespace

ProbeResult probe(std::string_view filename,
                  const std::uint8_t* prefix,
                  std::size_t prefix_size) noexcept {
    if (has_magic(prefix, prefix_size, 'S', 'C', 'M', ' ')) {
        return {Format::Scm, true, "application/vnd.dmc.scm"};
    }
    if (has_magic(prefix, prefix_size, 'M', 'O', 'D', ' ')) {
        return {Format::Mod, true, "application/vnd.dmc.mod"};
    }
    // Four-byte magic, not the superseded five-byte `HITS$`. Validated bytes
    // outrank the extension here: a HITS resource is routinely named `.ukn`.
    if (has_magic(prefix, prefix_size, 'H', 'I', 'T', 'S')) {
        return {Format::Hits, true, "application/vnd.dmc.hits"};
    }

    const auto ext = lower_extension(filename);
    if (ext == "scm") {
        return {Format::Unknown, false, "application/vnd.dmc.scm"};
    }
    if (ext == "mod") {
        return {Format::Unknown, false, "application/vnd.dmc.mod"};
    }
    if (ext == "hits") {
        return {Format::Unknown, false, "application/vnd.dmc.hits"};
    }

    return {};
}

const char* format_name(Format format) noexcept {
    switch (format) {
        case Format::Scm: return "SCM";
        case Format::Mod: return "MOD";
        case Format::Hits: return "HITS";
        default: return "UNKNOWN";
    }
}

}  // namespace dmcresource
