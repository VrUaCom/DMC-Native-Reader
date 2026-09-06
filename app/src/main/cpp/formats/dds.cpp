#include "dmcresource/formats/dds.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

namespace dmcresource::formats::dds {
namespace {

constexpr std::size_t kHeaderBytes = 128U;

[[nodiscard]] std::uint32_t read_u32_le(const std::uint8_t* p) noexcept {
    return static_cast<std::uint32_t>(p[0]) |
           (static_cast<std::uint32_t>(p[1]) << 8U) |
           (static_cast<std::uint32_t>(p[2]) << 16U) |
           (static_cast<std::uint32_t>(p[3]) << 24U);
}

[[nodiscard]] std::uint32_t full_mip_count(std::uint32_t width,
                                           std::uint32_t height) noexcept {
    std::uint32_t dimension = std::max(width, height);
    std::uint32_t count = 1U;
    while (dimension > 1U) {
        dimension /= 2U;
        ++count;
    }
    return count;
}

[[nodiscard]] bool payload_size(std::uint32_t width,
                                std::uint32_t height,
                                std::uint32_t mip_count,
                                std::uint32_t block_bytes,
                                std::uint32_t* out) noexcept {
    if (out == nullptr || width == 0U || height == 0U || mip_count == 0U) {
        return false;
    }

    std::uint64_t total = 0U;
    for (std::uint32_t level = 0U; level < mip_count; ++level) {
        const auto blocks_w = std::max(1U, (width + 3U) / 4U);
        const auto blocks_h = std::max(1U, (height + 3U) / 4U);
        const auto bytes = static_cast<std::uint64_t>(blocks_w) *
                           static_cast<std::uint64_t>(blocks_h) * block_bytes;
        if (total > std::numeric_limits<std::uint32_t>::max() - bytes) {
            return false;
        }
        total += bytes;
        width = std::max(1U, width / 2U);
        height = std::max(1U, height / 2U);
    }

    *out = static_cast<std::uint32_t>(total);
    return true;
}

[[nodiscard]] ParseResult reject(const char* diagnostic) noexcept {
    ParseResult out;
    out.diagnostic = diagnostic;
    return out;
}

}  // namespace

ParseResult parse(std::span<const std::uint8_t> bytes) noexcept {
    if (bytes.size() < kHeaderBytes) {
        return reject("DDS truncated header");
    }
    if (std::memcmp(bytes.data(), "DDS ", 4U) != 0) {
        return reject("DDS magic mismatch");
    }

    const auto header_size = read_u32_le(bytes.data() + 4U);
    const auto height = read_u32_le(bytes.data() + 12U);
    const auto width = read_u32_le(bytes.data() + 16U);
    const auto mip_count = read_u32_le(bytes.data() + 28U);
    const auto pixel_format_size = read_u32_le(bytes.data() + 76U);

    if (header_size != 124U || pixel_format_size != 32U) {
        return reject("DDS header structure mismatch");
    }
    if (width == 0U || height == 0U || mip_count == 0U) {
        return reject("DDS dimensions/mip count are invalid");
    }
    if (mip_count != full_mip_count(width, height)) {
        return reject("DDS mip chain is not complete for DMC3 HD reader contract");
    }

    Compression compression{};
    std::uint32_t block_bytes = 0U;
    const auto* fourcc = bytes.data() + 84U;
    if (std::memcmp(fourcc, "DXT1", 4U) == 0) {
        compression = Compression::Dxt1;
        block_bytes = 8U;
    } else if (std::memcmp(fourcc, "DXT5", 4U) == 0) {
        compression = Compression::Dxt5;
        block_bytes = 16U;
    } else {
        return reject("DDS compression is not supported by the DMC3 HD reader contract");
    }

    std::uint32_t payload = 0U;
    if (!payload_size(width, height, mip_count, block_bytes, &payload)) {
        return reject("DDS payload size overflow");
    }

    const auto total = static_cast<std::uint64_t>(kHeaderBytes) + payload;
    if (total > bytes.size() || total > std::numeric_limits<std::uint32_t>::max()) {
        return reject("DDS payload leaves bounded input span");
    }

    ParseResult out;
    out.ok = true;
    out.document.width = width;
    out.document.height = height;
    out.document.mip_count = mip_count;
    out.document.payload_size = payload;
    out.document.total_size = static_cast<std::uint32_t>(total);
    out.document.compression = compression;
    return out;
}

const char* compression_name(Compression compression) noexcept {
    switch (compression) {
    case Compression::Dxt1: return "DXT1";
    case Compression::Dxt5: return "DXT5";
    }
    return "unknown";
}

}  // namespace dmcresource::formats::dds
