#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>

#include "dmcresource/binary_reader.h"
#include "dmcresource/decode_pipeline.h"

namespace dmcresource::module_support {

// Normal diagnostic rejection may allocate strings/vector entries. It is an
// internal helper and deliberately propagates allocation failure to the nearest
// NativeModule/Spider noexcept boundary, where the failure is converted into a
// minimal no-allocation rejection instead of terminating the process.
[[nodiscard]] inline PipelineResult reject(const ProbeResult& probe,
                                           const char* module_id,
                                           std::string detail) {
    PipelineResult out;
    out.probe = probe;
    out.accepted = false;
    out.renderable = false;
    out.detail = std::move(detail);
    out.modules.push_back({"identity-probe", true});
    out.modules.push_back({"bounded-read-guard", true});
    out.modules.push_back({module_id, false});
    return out;
}

// Catch-path fallback: default std::string/vector state performs no dynamic
// allocation. Keep this helper noexcept so ABI callbacks can always fail closed
// even when diagnostics themselves cannot be allocated.
[[nodiscard]] inline PipelineResult reject_minimal(
    const ProbeResult& probe) noexcept {
    PipelineResult out;
    out.probe = probe;
    out.accepted = false;
    out.renderable = false;
    return out;
}

// Literal diagnostics are common on early guard/catch paths that already sit in
// noexcept adapters. Construct the std::string inside this boundary so an OOM
// becomes a minimal rejected result instead of escaping before reject() starts.
[[nodiscard]] inline PipelineResult reject(const ProbeResult& probe,
                                           const char* module_id,
                                           const char* detail) noexcept {
    try {
        return reject(
            probe, module_id,
            std::string{detail != nullptr ? detail : ""});
    } catch (...) {
        return reject_minimal(probe);
    }
}

[[nodiscard]] inline bool magic4(const BinaryReader& reader,
                                 std::size_t offset,
                                 char a,
                                 char b,
                                 char c,
                                 char d) noexcept {
    const auto* p = reader.ptr(offset, 4U);
    return p != nullptr &&
           p[0] == static_cast<std::uint8_t>(a) &&
           p[1] == static_cast<std::uint8_t>(b) &&
           p[2] == static_cast<std::uint8_t>(c) &&
           p[3] == static_cast<std::uint8_t>(d);
}

[[nodiscard]] inline bool zero_range(const BinaryReader& reader,
                                     std::size_t begin,
                                     std::size_t end) noexcept {
    if (begin > end || end > reader.size()) return false;
    const auto* p = reader.ptr(begin, end - begin);
    if (p == nullptr && begin != end) return false;
    for (std::size_t i = 0; i < end - begin; ++i) {
        if (p[i] != 0U) return false;
    }
    return true;
}

}  // namespace dmcresource::module_support
