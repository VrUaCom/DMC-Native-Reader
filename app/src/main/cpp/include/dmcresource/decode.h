#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "dmcresource/dmc_resource.h"
#include "dmcresource/mesh.h"

namespace dmcresource {

enum class DecodeStatus : std::uint8_t {
    Ok = 0,
    UnknownFormat,
    InvalidData,
    UnsupportedLayout,
    TooLarge,
};

struct DecodeResult {
    DecodeStatus status{DecodeStatus::UnknownFormat};
    Format format{Format::Unknown};
    Mesh mesh;
    const char* detail{"unknown format"};
    // Format-specific bounded structural summary.
    std::string info{};
    // Text payload for text-family resources. Geometry modules leave this empty.
    std::string text{};
};

const char* decode_status_name(DecodeStatus status) noexcept;

}  // namespace dmcresource
