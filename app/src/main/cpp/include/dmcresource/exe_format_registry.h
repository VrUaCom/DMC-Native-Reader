#pragma once

#include <string>
#include <string_view>

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

// Bounded human-readable form suitable for the Android diagnostic view.
std::string describe_exe_format_evidence(std::string_view family);

}  // namespace dmcresource
