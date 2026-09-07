#include "dmcresource/formats/dds.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

namespace dmcresource::formats::dds {
namespace {

constexpr std::size_t kHeaderBytes = 128U;

[[nodiscard]] std::uint16_t read_u16_le(const std::uint8_t* p) noexcept {
    return static_cast<std::uint16_t>(p[0]) |
           static_cast<std::uint16_t>(static_cast<std::uint16_t>(p[1]) << 8U);
}

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

[[nodiscard]] bool level_payload_size(std::uint32_t width,
                                      std::uint32_t height,
                                      std::uint32_t block_bytes,
                                      std::uint64_t* out) noexcept {
    if (out == nullptr || width == 0U || height == 0U || block_bytes == 0U) {
        return false;
    }

    const auto blocks_w = (static_cast<std::uint64_t>(width) + 3ULL) / 4ULL;
    const auto blocks_h = (static_cast<std::uint64_t>(height) + 3ULL) / 4ULL;
    if (blocks_w != 0U && blocks_h >
            std::numeric_limits<std::uint64_t>::max() / blocks_w) {
        return false;
    }
    const auto blocks = blocks_w * blocks_h;
    if (blocks > std::numeric_limits<std::uint64_t>::max() / block_bytes) {
        return false;
    }
    *out = blocks * block_bytes;
    return true;
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
        std::uint64_t bytes = 0U;
        if (!level_payload_size(width, height, block_bytes, &bytes) ||
            bytes > std::numeric_limits<std::uint32_t>::max() ||
            total > std::numeric_limits<std::uint32_t>::max() - bytes) {
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

[[nodiscard]] PreviewResult reject_preview(const char* diagnostic) noexcept {
    PreviewResult out;
    out.diagnostic = diagnostic;
    return out;
}

struct Rgba final {
    std::uint8_t r{};
    std::uint8_t g{};
    std::uint8_t b{};
    std::uint8_t a{255U};
};

[[nodiscard]] Rgba decode_565(std::uint16_t value) noexcept {
    const auto r5 = static_cast<std::uint32_t>((value >> 11U) & 0x1FU);
    const auto g6 = static_cast<std::uint32_t>((value >> 5U) & 0x3FU);
    const auto b5 = static_cast<std::uint32_t>(value & 0x1FU);
    return {
        static_cast<std::uint8_t>((r5 * 255U + 15U) / 31U),
        static_cast<std::uint8_t>((g6 * 255U + 31U) / 63U),
        static_cast<std::uint8_t>((b5 * 255U + 15U) / 31U),
        255U,
    };
}

[[nodiscard]] Rgba mix(const Rgba& a, const Rgba& b,
                       std::uint32_t wa, std::uint32_t wb,
                       std::uint32_t divisor) noexcept {
    return {
        static_cast<std::uint8_t>((wa * a.r + wb * b.r) / divisor),
        static_cast<std::uint8_t>((wa * a.g + wb * b.g) / divisor),
        static_cast<std::uint8_t>((wa * a.b + wb * b.b) / divisor),
        255U,
    };
}

void color_palette(const std::uint8_t* block, bool dxt1,
                   std::array<Rgba, 4U>* out) noexcept {
    const auto c0_raw = read_u16_le(block + 0U);
    const auto c1_raw = read_u16_le(block + 2U);
    const auto c0 = decode_565(c0_raw);
    const auto c1 = decode_565(c1_raw);
    (*out)[0] = c0;
    (*out)[1] = c1;

    if (!dxt1 || c0_raw > c1_raw) {
        (*out)[2] = mix(c0, c1, 2U, 1U, 3U);
        (*out)[3] = mix(c0, c1, 1U, 2U, 3U);
    } else {
        (*out)[2] = mix(c0, c1, 1U, 1U, 2U);
        (*out)[3] = {0U, 0U, 0U, 0U};
    }
}

void alpha_palette(const std::uint8_t* block,
                   std::array<std::uint8_t, 8U>* out) noexcept {
    const auto a0 = block[0];
    const auto a1 = block[1];
    (*out)[0] = a0;
    (*out)[1] = a1;
    if (a0 > a1) {
        (*out)[2] = static_cast<std::uint8_t>((6U * a0 + 1U * a1) / 7U);
        (*out)[3] = static_cast<std::uint8_t>((5U * a0 + 2U * a1) / 7U);
        (*out)[4] = static_cast<std::uint8_t>((4U * a0 + 3U * a1) / 7U);
        (*out)[5] = static_cast<std::uint8_t>((3U * a0 + 4U * a1) / 7U);
        (*out)[6] = static_cast<std::uint8_t>((2U * a0 + 5U * a1) / 7U);
        (*out)[7] = static_cast<std::uint8_t>((1U * a0 + 6U * a1) / 7U);
    } else {
        (*out)[2] = static_cast<std::uint8_t>((4U * a0 + 1U * a1) / 5U);
        (*out)[3] = static_cast<std::uint8_t>((3U * a0 + 2U * a1) / 5U);
        (*out)[4] = static_cast<std::uint8_t>((2U * a0 + 3U * a1) / 5U);
        (*out)[5] = static_cast<std::uint8_t>((1U * a0 + 4U * a1) / 5U);
        (*out)[6] = 0U;
        (*out)[7] = 255U;
    }
}

void write_pixel(ImagePreview* image, std::uint32_t x, std::uint32_t y,
                 const Rgba& rgba) noexcept {
    const auto index = (static_cast<std::size_t>(y) * image->width + x) * 4U;
    image->rgba8[index + 0U] = rgba.r;
    image->rgba8[index + 1U] = rgba.g;
    image->rgba8[index + 2U] = rgba.b;
    image->rgba8[index + 3U] = rgba.a;
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

PreviewResult decode_preview(std::span<const std::uint8_t> bytes,
                             const Document& document,
                             std::uint64_t max_pixels) noexcept {
    if (document.width == 0U || document.height == 0U ||
        document.total_size < kHeaderBytes || document.total_size > bytes.size()) {
        return reject_preview("DDS preview rejected invalid parsed document span");
    }

    const auto pixel_count = static_cast<std::uint64_t>(document.width) *
                             static_cast<std::uint64_t>(document.height);
    if (pixel_count == 0U || pixel_count > max_pixels ||
        pixel_count > std::numeric_limits<std::size_t>::max() / 4U) {
        return reject_preview("DDS preview exceeds bounded output pixel budget");
    }

    const std::uint32_t block_bytes =
        document.compression == Compression::Dxt1 ? 8U : 16U;
    std::uint64_t base_bytes = 0U;
    if (!level_payload_size(document.width, document.height, block_bytes, &base_bytes) ||
        base_bytes > bytes.size() - kHeaderBytes) {
        return reject_preview("DDS preview base mip leaves bounded input span");
    }

    PreviewResult out;
    out.image.width = document.width;
    out.image.height = document.height;
    try {
        out.image.rgba8.assign(static_cast<std::size_t>(pixel_count * 4U), 0U);
    } catch (...) {
        return reject_preview("DDS preview allocation failed");
    }

    const auto blocks_w = (static_cast<std::uint64_t>(document.width) + 3ULL) / 4ULL;
    const auto blocks_h = (static_cast<std::uint64_t>(document.height) + 3ULL) / 4ULL;
    const auto* base = bytes.data() + kHeaderBytes;

    for (std::uint64_t by = 0U; by < blocks_h; ++by) {
        for (std::uint64_t bx = 0U; bx < blocks_w; ++bx) {
            const auto block_index = by * blocks_w + bx;
            const auto* block = base + static_cast<std::size_t>(block_index * block_bytes);

            std::array<Rgba, 4U> colors{};
            std::uint32_t color_indices = 0U;
            std::array<std::uint8_t, 8U> alphas{};
            std::uint64_t alpha_indices = 0U;

            if (document.compression == Compression::Dxt1) {
                color_palette(block, true, &colors);
                color_indices = read_u32_le(block + 4U);
            } else {
                alpha_palette(block, &alphas);
                for (std::uint32_t i = 0U; i < 6U; ++i) {
                    alpha_indices |= static_cast<std::uint64_t>(block[2U + i]) << (i * 8U);
                }
                color_palette(block + 8U, false, &colors);
                color_indices = read_u32_le(block + 12U);
            }

            for (std::uint32_t py = 0U; py < 4U; ++py) {
                for (std::uint32_t px = 0U; px < 4U; ++px) {
                    const auto x64 = bx * 4ULL + px;
                    const auto y64 = by * 4ULL + py;
                    if (x64 >= document.width || y64 >= document.height) continue;

                    const auto local = py * 4U + px;
                    const auto color_code = (color_indices >> (local * 2U)) & 0x3U;
                    auto rgba = colors[color_code];
                    if (document.compression == Compression::Dxt5) {
                        const auto alpha_code = static_cast<std::uint32_t>(
                            (alpha_indices >> (local * 3U)) & 0x7ULL);
                        rgba.a = alphas[alpha_code];
                    }
                    write_pixel(&out.image,
                                static_cast<std::uint32_t>(x64),
                                static_cast<std::uint32_t>(y64), rgba);
                }
            }
        }
    }

    out.ok = out.image.available();
    if (!out.ok) out.diagnostic = "DDS preview output failed self-validation";
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
