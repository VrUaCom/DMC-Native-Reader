#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

#include "dmcresource/image_preview.h"

namespace dmcresource::formats::dds {

enum class Compression : std::uint8_t {
    Dxt1,
    Dxt5,
};

struct Document final {
    std::uint32_t width{};
    std::uint32_t height{};
    std::uint32_t mip_count{};
    std::uint32_t payload_size{};
    std::uint32_t total_size{};
    Compression compression{Compression::Dxt1};
};

struct ParseResult final {
    bool ok{};
    Document document;
    std::string diagnostic;
};

struct PreviewResult final {
    bool ok{};
    ImagePreview image;
    std::string diagnostic;
};

[[nodiscard]] ParseResult parse(std::span<const std::uint8_t> bytes) noexcept;

// Decodes only mip level 0 into the generic static preview contract. The
// parser remains the structural authority; this helper never changes DDS
// acceptance semantics and applies an explicit output-pixel allocation cap.
[[nodiscard]] PreviewResult decode_preview(
    std::span<const std::uint8_t> bytes,
    const Document& document,
    std::uint64_t max_pixels = 16ULL * 1024ULL * 1024ULL) noexcept;

[[nodiscard]] const char* compression_name(Compression compression) noexcept;

}  // namespace dmcresource::formats::dds
