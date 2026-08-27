#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace dmcresource {

enum class Format : std::uint8_t {
    Unknown = 0,
    Scm,
    Mod,
    Hits,
};

struct ProbeResult {
    Format format{Format::Unknown};
    bool magic_confirmed{false};
    const char* mime_type{"application/octet-stream"};
};

ProbeResult probe(std::string_view filename,
                  const std::uint8_t* prefix,
                  std::size_t prefix_size) noexcept;

const char* format_name(Format format) noexcept;

}  // namespace dmcresource
