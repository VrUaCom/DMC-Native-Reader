#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace dmcresource {

// Clean Native Reader 1.0 format surface. Families are added only after they
// have a bounded portable ReaderCore contract.
enum class Format : std::uint8_t {
    Unknown = 0,
    Scm,
    Mod,
    Dds,
    Ptx,
    Evt,
    Pac,
    Mot,
    Pnst,
    Shw,
};

struct ProbeResult {
    Format format{Format::Unknown};
    bool recognized{false};
    bool content_confirmed{false};
    const char* family{"UNKNOWN"};
    const char* domain{"unknown"};
    const char* support{"none"};
    const char* evidence{"unknown"};
    const char* mime_type{"application/octet-stream"};
};

// Extension routing normalizes a std::string and can allocate. The decode
// pipeline owns the fail-closed exception boundary; do not advertise noexcept
// on this allocating helper.
ProbeResult probe(std::string_view filename,
                  const std::uint8_t* bytes,
                  std::size_t size);

std::string describe_resource(std::string_view filename,
                              const std::uint8_t* bytes,
                              std::size_t size,
                              const ProbeResult& probe_result);

const char* format_name(Format format) noexcept;

}  // namespace dmcresource
