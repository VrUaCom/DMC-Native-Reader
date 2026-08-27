#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

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
    // Format-specific structural summary, empty when the format has none.
    // The default member initializer keeps the existing four-element aggregate
    // initializers in the SCM/MOD decoder valid and warning-free.
    std::string info{};
};

DecodeResult decode_resource(std::string_view filename,
                             const std::uint8_t* bytes,
                             std::size_t size) noexcept;

const char* decode_status_name(DecodeStatus status) noexcept;

}  // namespace dmcresource
