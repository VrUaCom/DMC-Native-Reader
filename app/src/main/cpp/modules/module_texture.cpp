#include "dmcresource/native_module.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <sstream>
#include <string>

#include "dmcresource/binary_reader.h"

namespace dmcresource {
namespace {

constexpr std::size_t kDdsHeaderBytes = 128u;
constexpr std::size_t kPtxHeaderBytes = 0x800u;
constexpr std::size_t kPtxDescriptorBytes = 0x70u;
constexpr std::size_t kSectorBytes = 0x800u;
constexpr std::uint32_t kMaxTextureCount = 4096u;

bool magic4(const BinaryReader& reader, std::size_t offset,
            char a, char b, char c, char d) noexcept {
    const auto* p = reader.ptr(offset, 4u);
    return p != nullptr &&
           p[0] == static_cast<std::uint8_t>(a) &&
           p[1] == static_cast<std::uint8_t>(b) &&
           p[2] == static_cast<std::uint8_t>(c) &&
           p[3] == static_cast<std::uint8_t>(d);
}

std::uint32_t full_mip_count(std::uint32_t width,
                             std::uint32_t height) noexcept {
    std::uint32_t dimension = std::max(width, height);
    std::uint32_t count = 1u;
    while (dimension > 1u) {
        dimension /= 2u;
        ++count;
    }
    return count;
}

bool dxt_payload_size(std::uint32_t width, std::uint32_t height,
                      std::uint32_t mip_count, std::uint32_t block_bytes,
                      std::uint32_t* out) noexcept {
    if (out == nullptr || width == 0u || height == 0u || mip_count == 0u) {
        return false;
    }
    std::uint64_t total = 0u;
    for (std::uint32_t level = 0u; level < mip_count; ++level) {
        const auto blocks_w = std::max(1u, (width + 3u) / 4u);
        const auto blocks_h = std::max(1u, (height + 3u) / 4u);
        const auto bytes = static_cast<std::uint64_t>(blocks_w) *
                           static_cast<std::uint64_t>(blocks_h) * block_bytes;
        if (total > std::numeric_limits<std::uint32_t>::max() - bytes) return false;
        total += bytes;
        width = std::max(1u, width / 2u);
        height = std::max(1u, height / 2u);
    }
    *out = static_cast<std::uint32_t>(total);
    return true;
}

struct DdsInfo {
    bool ok{};
    std::uint32_t width{};
    std::uint32_t height{};
    std::uint32_t mips{};
    std::uint32_t payload{};
    const char* fourcc{"????"};
};

DdsInfo inspect_dds(const BinaryReader& reader, std::size_t offset,
                    std::size_t bounded_end) noexcept {
    DdsInfo out;
    if (bounded_end > reader.size() || offset > bounded_end ||
        bounded_end - offset < kDdsHeaderBytes ||
        !magic4(reader, offset, 'D', 'D', 'S', ' ')) {
        return out;
    }

    std::uint32_t header_size = 0u;
    std::uint32_t height = 0u;
    std::uint32_t width = 0u;
    std::uint32_t mips = 0u;
    std::uint32_t pf_size = 0u;
    if (!reader.read_le(offset + 4u, &header_size) ||
        !reader.read_le(offset + 12u, &height) ||
        !reader.read_le(offset + 16u, &width) ||
        !reader.read_le(offset + 28u, &mips) ||
        !reader.read_le(offset + 76u, &pf_size)) {
        return out;
    }
    if (header_size != 124u || pf_size != 32u || width == 0u || height == 0u ||
        mips == 0u || mips != full_mip_count(width, height)) {
        return out;
    }

    const auto* fourcc = reader.ptr(offset + 84u, 4u);
    if (fourcc == nullptr) return out;
    std::uint32_t block_bytes = 0u;
    if (fourcc[0] == 'D' && fourcc[1] == 'X' && fourcc[2] == 'T' && fourcc[3] == '1') {
        block_bytes = 8u;
        out.fourcc = "DXT1";
    } else if (fourcc[0] == 'D' && fourcc[1] == 'X' && fourcc[2] == 'T' && fourcc[3] == '5') {
        block_bytes = 16u;
        out.fourcc = "DXT5";
    } else {
        return out;
    }

    if (!dxt_payload_size(width, height, mips, block_bytes, &out.payload)) return out;
    if (static_cast<std::uint64_t>(offset) + kDdsHeaderBytes + out.payload > bounded_end) {
        return out;
    }
    out.width = width;
    out.height = height;
    out.mips = mips;
    out.ok = true;
    return out;
}

PipelineResult rejected(const ProbeResult& probe, const char* module_id,
                        const char* reason) noexcept {
    PipelineResult out;
    out.probe = probe;
    out.accepted = false;
    out.renderable = false;
    out.detail = reason;
    out.modules.push_back({"identity-probe", true});
    out.modules.push_back({"bounded-read-guard", true});
    out.modules.push_back({module_id, false});
    return out;
}

PipelineResult run_dds(std::string_view,
                       const std::uint8_t* bytes,
                       std::size_t size,
                       const ProbeResult& probe) noexcept {
    const BinaryReader reader(bytes, size);
    const auto dds = inspect_dds(reader, 0u, size);
    if (!dds.ok || kDdsHeaderBytes + dds.payload != size) {
        return rejected(probe, "formats.dds.dmc3-reader",
                        "DDS rejected: expected a bounded complete DXT1/DXT5 full mip chain");
    }
    std::ostringstream detail;
    detail << "DDS " << dds.fourcc << " " << dds.width << "x" << dds.height
           << " mips=" << dds.mips << " payload=" << dds.payload;
    return structural_pipeline(probe, "formats.dds.dmc3-reader", detail.str());
}

bool zero_range(const BinaryReader& reader, std::size_t begin,
                std::size_t end) noexcept {
    if (begin > end || end > reader.size()) return false;
    const auto* p = reader.ptr(begin, end - begin);
    if (p == nullptr && begin != end) return false;
    for (std::size_t i = 0; i < end - begin; ++i) {
        if (p[i] != 0u) return false;
    }
    return true;
}

PipelineResult run_ptx(std::string_view,
                       const std::uint8_t* bytes,
                       std::size_t size,
                       const ProbeResult& probe) noexcept {
    const BinaryReader reader(bytes, size);
    if (!reader.range(0u, kPtxHeaderBytes)) {
        return rejected(probe, "formats.ptx.bundle-reader",
                        "PTX rejected: resource is shorter than the 0x800-byte bundle header");
    }
    std::uint32_t count = 0u;
    if (!reader.read_le(0u, &count) || count == 0u || count > kMaxTextureCount ||
        count > (kPtxHeaderBytes - 4u) / 4u) {
        return rejected(probe, "formats.ptx.bundle-reader",
                        "PTX rejected: texture count is invalid");
    }

    std::size_t descriptor = kPtxHeaderBytes;
    std::uint64_t total_dds_bytes = 0u;
    std::uint32_t dxt1 = 0u;
    std::uint32_t dxt5 = 0u;
    for (std::uint32_t index = 0u; index < count; ++index) {
        std::uint32_t sector_span = 0u;
        if (!reader.read_le(4u + static_cast<std::size_t>(index) * 4u, &sector_span)) {
            return rejected(probe, "formats.ptx.bundle-reader",
                            "PTX rejected: sector-span table is truncated");
        }
        const bool final = index + 1u == count;
        std::size_t bounded_end = size;
        if (!final || sector_span != 0u) {
            if (sector_span == 0u || sector_span > std::numeric_limits<std::size_t>::max() / kSectorBytes) {
                return rejected(probe, "formats.ptx.bundle-reader",
                                "PTX rejected: invalid sector span");
            }
            const auto span_bytes = static_cast<std::size_t>(sector_span) * kSectorBytes;
            if (descriptor > size || span_bytes > size - descriptor) {
                return rejected(probe, "formats.ptx.bundle-reader",
                                "PTX rejected: sector span leaves resource bounds");
            }
            bounded_end = descriptor + span_bytes;
            if (final && bounded_end != size) {
                return rejected(probe, "formats.ptx.bundle-reader",
                                "PTX rejected: final sector span does not terminate at EOF");
            }
        }
        if (!reader.range(descriptor, kPtxDescriptorBytes)) {
            return rejected(probe, "formats.ptx.bundle-reader",
                            "PTX rejected: descriptor is truncated");
        }
        const std::size_t dds_offset = descriptor + kPtxDescriptorBytes;
        const auto dds = inspect_dds(reader, dds_offset, bounded_end);
        if (!dds.ok) {
            return rejected(probe, "formats.ptx.bundle-reader",
                            "PTX rejected: descriptor is not followed by a valid DXT1/DXT5 DDS");
        }
        std::uint32_t descriptor_payload = 0u;
        std::uint32_t descriptor_dds_size = 0u;
        if (!reader.read_le(descriptor + 0x38u, &descriptor_payload) ||
            !reader.read_le(descriptor + 0x64u, &descriptor_dds_size) ||
            descriptor_payload != dds.payload ||
            descriptor_dds_size != kDdsHeaderBytes + dds.payload) {
            return rejected(probe, "formats.ptx.bundle-reader",
                            "PTX rejected: descriptor DDS sizes disagree with mip payload");
        }
        const std::size_t dds_end = dds_offset + descriptor_dds_size;
        if (dds_end > bounded_end) {
            return rejected(probe, "formats.ptx.bundle-reader",
                            "PTX rejected: DDS escapes its descriptor span");
        }
        if (final && sector_span == 0u) {
            if (dds_end != size) {
                return rejected(probe, "formats.ptx.bundle-reader",
                                "PTX rejected: zero-span final DDS does not end at EOF");
            }
        } else if (!zero_range(reader, dds_end, bounded_end)) {
            return rejected(probe, "formats.ptx.bundle-reader",
                            "PTX rejected: alignment padding contains non-zero data");
        }
        total_dds_bytes += descriptor_dds_size;
        if (std::string_view{dds.fourcc} == "DXT1") ++dxt1; else ++dxt5;
        if (!final) descriptor = bounded_end;
    }

    std::ostringstream detail;
    detail << "PTX texture bundle | textures=" << count
           << " dxt1=" << dxt1 << " dxt5=" << dxt5
           << " ddsBytes=" << total_dds_bytes;
    auto out = structural_pipeline(probe, "formats.ptx.bundle-reader", detail.str());
    out.modules.insert(out.modules.begin() + 3,
                       {"formats.dds.child-validation", true});
    return out;
}

}  // namespace

NativeModule dds_module() noexcept {
    return {"formats.dds.dmc3-reader", "DDS", Format::Dds,
            ModuleKind::Structural, false, run_dds};
}

NativeModule ptx_module() noexcept {
    return {"formats.ptx.bundle-reader", "PTX", Format::Ptx,
            ModuleKind::Structural, false, run_ptx};
}

}  // namespace dmcresource
