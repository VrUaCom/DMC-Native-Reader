#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace dmcresource {

// Clean Native Reader 1.0 format surface. Other DMC families are intentionally
// not registered in main until they are promoted to the same modular contract.
enum class Format : std::uint8_t {
    Unknown = 0,
    Scm,
    Mod,
    Dds,
    Ptx,
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

ProbeResult probe(std::string_view filename,
                  const std::uint8_t* bytes,
                  std::size_t size) noexcept;

std::string describe_resource(std::string_view filename,
                              const std::uint8_t* bytes,
                              std::size_t size,
                              const ProbeResult& probe_result);

const char* format_name(Format format) noexcept;

}  // namespace dmcresource
