#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace dmcresource {

// Generic static image-preview contract. Format modules may populate this
// without introducing format-specific Android viewers or JNI methods.
struct ImagePreview final {
    std::uint32_t width{};
    std::uint32_t height{};
    std::vector<std::uint8_t> rgba8;

    [[nodiscard]] bool available() const noexcept {
        if (width == 0U || height == 0U) return false;
        const auto pixels = static_cast<std::uint64_t>(width) *
                            static_cast<std::uint64_t>(height);
        if (pixels > std::numeric_limits<std::size_t>::max() / 4U) return false;
        return rgba8.size() == static_cast<std::size_t>(pixels * 4U);
    }
};

}  // namespace dmcresource
