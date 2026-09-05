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
    // Format-specific bounded structural summary.
    std::string info{};
    // Text payload for text-family resources. Geometry modules leave this empty.
    std::string text{};
};

// Compatibility entry point. New product code dispatches through
// NativeModuleRegistry; these format-specific functions are the module backends.
DecodeResult decode_resource(std::string_view filename,
                             const std::uint8_t* bytes,
                             std::size_t size) noexcept;
DecodeResult decode_scm(const std::uint8_t* bytes, std::size_t size) noexcept;
DecodeResult decode_mod(const std::uint8_t* bytes, std::size_t size) noexcept;

const char* decode_status_name(DecodeStatus status) noexcept;

}  // namespace dmcresource
