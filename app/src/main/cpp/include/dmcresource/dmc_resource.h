#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace dmcresource {

// Format controls the modular decode pipeline.  MOD/SCM currently reach a
// complete static-mesh stage.  EFM/MRP/SHW have explicit adapters so their
// decode gaps can be closed module-by-module without duplicating the shared
// binary/model core.  Other covers the wider DMC3 catalog.
enum class Format : std::uint8_t {
    Unknown = 0,
    Scm,
    Mod,
    Efm,
    Mrp,
    Shw,
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
