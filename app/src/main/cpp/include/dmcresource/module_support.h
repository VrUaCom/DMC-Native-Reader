#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "dmcresource/binary_reader.h"
#include "dmcresource/decode_pipeline.h"

namespace dmcresource::module_support {

[[nodiscard]] inline PipelineResult reject(const ProbeResult& probe,
                                           const char* module_id,
                                           std::string detail) noexcept {
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
