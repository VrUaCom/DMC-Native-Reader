#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "dmcresource/dmc_resource.h"

namespace dmcresource {

// Evidence about a resource family observed directly in the canonical DMC3 HD
// executable (SHA-256 e454272e...d082).  This is intentionally separate from
// semantic decoding support: runtime recognition does not prove a full schema.
struct ExeFormatEvidence {
    bool present{false};
    const char* strength{"none"};
    const char* path{"none"};
    const char* detail{"not present in canonical EXE evidence registry"};
};

// Accepts Native Reader family labels (for example "TIM2", "AFS namespace",
// ".bin") and normalizes aliases before lookup.
ExeFormatEvidence exe_format_evidence(std::string_view family) noexcept;

// Mirrors only the bounded content-family identities proved by the canonical
// executable.  It is not a general format parser.  In particular the primary
// registry probe checks only bytes 0..2 for MOD/EFM/SCM/MRP/SHW, whereas MCV
// belongs to the exact four-byte family-mask path and requires "MCV ".
//
// Canonical MOD<space>/SCM<space> remain the validated mesh decoder inputs.  A
// non-space fourth byte can therefore be runtime-recognized here while staying
// Format::Other so Native Reader does not fabricate/attempt a mesh layout.
ProbeResult probe_exe_runtime_identity(const std::uint8_t* bytes,
                                       std::size_t size) noexcept;

// Bounded human-readable form suitable for the Android diagnostic view.
std::string describe_exe_format_evidence(std::string_view family);

}  // namespace dmcresource
