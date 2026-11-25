#include "dmcresource/ptx_rgba.h"

#include <algorithm>
#include <bit>
#include <cstring>

#include "dmcresource/dds_bcn.h"
#include "dmcresource/dds_bcn_encode.h"
#include "dmcresource/resource_limits.h"

namespace dmcresource::ptx_rgba {
namespace {

constexpr std::size_t kSector = 0x800U;
constexpr std::size_t kDescriptor = 0x70U;
constexpr std::size_t kDdsHeader = 128U;
constexpr std::size_t kDescriptorDdsSize = 0x64U;
constexpr std::uint32_t kMaxTextures = 256U;
constexpr std::uint32_t kPixelFourcc = 0x4U;
constexpr std::uint32_t kPixelRgb = 0x40U;
constexpr std::uint32_t kPixelAlpha = 0x1U;

std::uint32_t u32(std::span<const std::uint8_t> s, std::size_t at) noexcept {
    std::uint32_t value = 0;
    std::memcpy(&value, s.data() + at, 4U);
    return value;
}

void put_u32(std::vector<std::uint8_t>& s, std::size_t at, std::uint32_t value) noexcept {
    std::memcpy(s.data() + at, &value, 4U);
}

// One channel of a pixel by its DDS mask, scaled to 8 bits.
std::uint8_t channel(std::uint32_t pixel, std::uint32_t mask) noexcept {
    if (mask == 0U) return 0U;
    const int shift = std::countr_zero(mask);
    const std::uint32_t max = mask >> shift;
    const std::uint32_t value = (pixel & mask) >> shift;
    return static_cast<std::uint8_t>(max == 255U ? value : (value * 255U + max / 2U) / max);
}

struct Record final {
    std::span<const std::uint8_t> descriptor;  // 0x70 bytes
    std::span<const std::uint8_t> dds;          // the DDS as stored
    std::vector<std::uint8_t> encoded;          // the BC3 DDS when it was uncompressed
};

// The DDS of an uncompressed texture as BC3 with as many levels; nullopt when
// it is no uncompressed RGB(A) DDS this reader takes.
std::optional<std::vector<std::uint8_t>> encode_uncompressed(std::span<const std::uint8_t> dds) {
    if (dds.size() < kDdsHeader || std::memcmp(dds.data(), "DDS ", 4U) != 0) return std::nullopt;
    const auto height = u32(dds, 12U);
    const auto width = u32(dds, 16U);
    const auto mips = std::max<std::uint32_t>(u32(dds, 28U), 1U);
    const auto flags = u32(dds, 80U);
    const auto bits = u32(dds, 88U);
    if ((flags & kPixelFourcc) != 0U || (flags & kPixelRgb) == 0U || (bits != 32U && bits != 24U)) return std::nullopt;
    if (width == 0U || height == 0U || width > 8192U || height > 8192U || mips > 16U) return std::nullopt;
    const std::uint64_t bytes_per_pixel = bits / 8U;
    const std::uint64_t level0 = std::uint64_t{width} * height * bytes_per_pixel;
    if (kDdsHeader + level0 > dds.size()) return std::nullopt;
    const auto r = u32(dds, 92U);
    const auto g = u32(dds, 96U);
    const auto b = u32(dds, 100U);
    const auto a = (flags & kPixelAlpha) != 0U ? u32(dds, 104U) : 0U;

    dds_bcn::RgbaImage image;
    image.width = width;
    image.height = height;
    image.rgba8.resize(std::size_t{width} * height * 4U);
    const auto* in = dds.data() + kDdsHeader;
    for (std::size_t p = 0; p < std::size_t{width} * height; ++p) {
        std::uint32_t pixel = 0;
        std::memcpy(&pixel, in + p * bytes_per_pixel, bytes_per_pixel);
        auto* out = image.rgba8.data() + p * 4U;
        out[0] = channel(pixel, r);
        out[1] = channel(pixel, g);
        out[2] = channel(pixel, b);
        out[3] = a != 0U ? channel(pixel, a) : 255U;
    }
    // The same mip count, made from level 0 (a 2x2 box filter per level).
    std::vector<dds_bcn::RgbaImage> levels;
    levels.push_back(std::move(image));
    while (levels.size() < mips && (levels.back().width > 1U || levels.back().height > 1U)) {
        levels.push_back(dds_bcn::downsample(levels.back()));
    }
    const auto encoded = dds_bcn::encode_dds(dds_bcn::Format::bc3, levels);
    if (!encoded.ok) return std::nullopt;
    std::vector<std::uint8_t> out(encoded.bytes.size());
    std::memcpy(out.data(), encoded.bytes.data(), out.size());
    return out;
}

}  // namespace

std::optional<std::vector<std::uint8_t>> compress(std::span<const std::uint8_t> s) {
    try {
        if (s.size() < kSector * 2U || s.size() % kSector != 0U) return std::nullopt;
        const auto count = u32(s, 0U);
        if (count == 0U || count > kMaxTextures || 4U + std::size_t{count} * 4U > kSector) return std::nullopt;
        // The header sector holds the count and the spans, then zeros.
        for (std::size_t i = 4U + std::size_t{count} * 4U; i < kSector; ++i) {
            if (s[i] != 0U) return std::nullopt;
        }

        std::vector<Record> records;
        std::size_t changed = 0;
        std::uint64_t sector = 1U;
        for (std::uint32_t index = 0; index < count; ++index) {
            const auto span = u32(s, 4U + std::size_t{index} * 4U);
            const std::uint64_t start = sector * kSector;
            const std::uint64_t end = start + std::uint64_t{span} * kSector;
            if (span == 0U || end > s.size()) return std::nullopt;
            const auto dds_at = static_cast<std::size_t>(start + kDescriptor);
            if (dds_at + kDdsHeader > end || std::memcmp(s.data() + dds_at, "DDS ", 4U) != 0) return std::nullopt;
            const std::uint64_t stored = u32(s, static_cast<std::size_t>(start) + kDescriptorDdsSize);
            const std::uint64_t room = end - dds_at;
            Record record;
            record.descriptor = s.subspan(static_cast<std::size_t>(start), kDescriptor);
            record.dds = s.subspan(dds_at, static_cast<std::size_t>(stored >= kDdsHeader && stored <= room ? stored : room));
            if (auto encoded = encode_uncompressed(record.dds)) {
                record.encoded = std::move(*encoded);
                ++changed;
            }
            records.push_back(std::move(record));
            sector += span;
        }
        if (sector * kSector != s.size() || changed == 0U) return std::nullopt;

        std::vector<std::uint8_t> out(kSector, 0U);
        put_u32(out, 0U, count);
        for (std::size_t index = 0; index < records.size(); ++index) {
            const auto& record = records[index];
            const auto dds = record.encoded.empty() ? record.dds : std::span<const std::uint8_t>{record.encoded};
            const std::size_t start = out.size();
            out.insert(out.end(), record.descriptor.begin(), record.descriptor.end());
            out.insert(out.end(), dds.begin(), dds.end());
            put_u32(out, start + kDescriptorDdsSize, static_cast<std::uint32_t>(dds.size()));
            out.resize((out.size() + kSector - 1U) / kSector * kSector, 0U);
            put_u32(out, 4U + index * 4U, static_cast<std::uint32_t>((out.size() - start) / kSector));
            if (out.size() > resource_limits::kMaxResourceBytes) return std::nullopt;
        }
        return out;
    } catch (...) {
        return std::nullopt;
    }
}

}  // namespace dmcresource::ptx_rgba
