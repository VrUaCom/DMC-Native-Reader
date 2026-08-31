#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace dmcresource {

// Format controls the native decode path.  Only SCM/MOD currently materialize
// a Mesh; Other covers catalogued DMC resource families that can be recognized
// and inspected without pretending they share the SCM/MOD layout.
enum class Format : std::uint8_t {
    Unknown = 0,
    Scm,
    Mod,
    Other,
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

// Produces a bounded human-readable structural summary.  This is deliberately
// separate from mesh decoding so recognition never implies a fake 3D layout.
std::string describe_resource(std::string_view filename,
                              const std::uint8_t* bytes,
                              std::size_t size,
                              const ProbeResult& probe_result);

const char* format_name(Format format) noexcept;

}  // namespace dmcresource
