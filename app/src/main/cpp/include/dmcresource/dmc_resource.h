#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace dmcresource {

// Stable resource identities used by the modular Native Reader. Recognition
// and decoding are deliberately separate: a format may be recognized while its
// module remains structural/inspection-only.
enum class Format : std::uint8_t {
    Unknown = 0,
    Pe,
    Scm,
    Mod,
    Efm,
    Mrp,
    Shw,
    Hits,
    StageTxt,
    Index,
    Dds,
    Ptx,
    Dca,
    Lig,
    Lig2,
    Pac,
    Pnst,
    Nbz,
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

// Produces a bounded human-readable structural summary. This is deliberately
// separate from decode modules so recognition never implies fake semantics.
std::string describe_resource(std::string_view filename,
                              const std::uint8_t* bytes,
                              std::size_t size,
                              const ProbeResult& probe_result);

const char* format_name(Format format) noexcept;

}  // namespace dmcresource
