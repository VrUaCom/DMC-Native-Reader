#include "dmcresource/native_module.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

#include "dmcresource/binary_reader.h"
#include "dmcresource/child_resource.h"
#include "dmcresource/formats/dds.h"
#include "dmcresource/module_support.h"

namespace dmcresource {
namespace {

constexpr std::size_t kPtxHeaderBytes = 0x800U;
constexpr std::size_t kPtxDescriptorBytes = 0x70U;
constexpr std::size_t kSectorBytes = 0x800U;
constexpr std::uint32_t kMaxTextureCount = 4096U;
constexpr std::uint64_t kMaxPtxGalleryPreviewPixels = 4ULL * 1024ULL * 1024ULL;

InspectionNode dds_inspection_node(const formats::dds::Document& dds,
                                   std::string id,
                                   std::string title,
                                   std::size_t offset) {
    InspectionNode node;
    node.id = std::move(id);
    node.title = std::move(title);
    node.kind = InspectionKind::Texture;
    node.source_span = SourceSpan{offset, dds.total_size};
    node.properties.push_back({"Compression",
                               formats::dds::compression_name(dds.compression),
                               EvidenceLevel::DataConfirmed});
    node.properties.push_back({"Width", std::to_string(dds.width),
                               EvidenceLevel::StructuralConfirmed});
    node.properties.push_back({"Height", std::to_string(dds.height),
                               EvidenceLevel::StructuralConfirmed});
    node.properties.push_back({"MipCount", std::to_string(dds.mip_count),
                               EvidenceLevel::StructuralConfirmed});
    node.properties.push_back({"PayloadBytes", std::to_string(dds.payload_size),
                               EvidenceLevel::StructuralConfirmed});
    return node;
}

std::string dds_detail(const formats::dds::Document& dds) {
    std::ostringstream detail;
    detail << "DDS " << formats::dds::compression_name(dds.compression)
           << " " << dds.width << "x" << dds.height
           << " mips=" << dds.mip_count
           << " payload=" << dds.payload_size;
    return detail.str();
}

ProbeResult child_dds_probe() noexcept {
    return {Format::Dds, true, true, "DDS", "texture", "recognized",
            "DATA_CONFIRMED", "image/vnd-ms.dds"};
}

struct LocatedDds final {
    bool ok{};
    formats::dds::Document document;
    std::span<const std::uint8_t> bytes;
    std::size_t source_offset{};
    std::size_t trailing_padding{};
    bool descriptor_wrapped{};
    std::string diagnostic;
};

LocatedDds locate_top_level_dds(const std::uint8_t* bytes,
                                std::size_t size) noexcept {
    LocatedDds out;
    if (bytes == nullptr) {
        out.diagnostic = "DDS rejected: null input";
        return out;
    }

    const BinaryReader reader(bytes, size);
    const auto direct_span = std::span<const std::uint8_t>{bytes, size};
    const auto direct = formats::dds::parse(direct_span);
    if (direct.ok) {
        const auto end = static_cast<std::size_t>(direct.document.total_size);
        if (!module_support::zero_range(reader, end, size)) {
            out.diagnostic = "DDS rejected: non-zero bytes follow the bounded DDS payload";
            return out;
        }
        out.ok = true;
        out.document = direct.document;
        out.bytes = std::span<const std::uint8_t>{bytes, end};
        out.trailing_padding = size - end;
        return out;
    }

    // DMC texture extraction can expose the canonical 0x70-byte texture
    // descriptor together with the DDS payload. The same descriptor shape is
    // already validated inside PTX: +0x38 is payload bytes and +0x64 is total
    // DDS bytes. Accept it only when both fields agree exactly with the parsed
    // embedded DDS and all bytes after that DDS are zero padding.
    if (size >= kPtxDescriptorBytes + 128U &&
        module_support::magic4(reader, kPtxDescriptorBytes,
                               'D', 'D', 'S', ' ')) {
        const auto embedded_span = std::span<const std::uint8_t>{
            bytes + kPtxDescriptorBytes, size - kPtxDescriptorBytes};
        const auto embedded = formats::dds::parse(embedded_span);
        if (!embedded.ok) {
            out.diagnostic = "DDS descriptor carrier rejected: " + embedded.diagnostic;
            return out;
        }

        std::uint32_t descriptor_payload = 0U;
        std::uint32_t descriptor_dds_size = 0U;
        if (!reader.read_le(0x38U, &descriptor_payload) ||
            !reader.read_le(0x64U, &descriptor_dds_size) ||
            descriptor_payload != embedded.document.payload_size ||
            descriptor_dds_size != embedded.document.total_size) {
            out.diagnostic =
                "DDS descriptor carrier rejected: descriptor sizes disagree with embedded DDS";
            return out;
        }

        const auto dds_end = kPtxDescriptorBytes +
            static_cast<std::size_t>(embedded.document.total_size);
        if (dds_end > size || !module_support::zero_range(reader, dds_end, size)) {
            out.diagnostic =
                "DDS descriptor carrier rejected: non-zero bytes follow embedded DDS";
            return out;
        }

        out.ok = true;
        out.document = embedded.document;
        out.bytes = std::span<const std::uint8_t>{
            bytes + kPtxDescriptorBytes,
            static_cast<std::size_t>(embedded.document.total_size)};
        out.source_offset = kPtxDescriptorBytes;
        out.trailing_padding = size - dds_end;
        out.descriptor_wrapped = true;
        return out;
    }

    out.diagnostic = direct.diagnostic.empty()
        ? "DDS rejected: no valid DDS payload found"
        : "DDS rejected: " + direct.diagnostic;
    return out;
}

PipelineResult run_dds(std::string_view,
                       const std::uint8_t* bytes,
                       std::size_t size,
                       const ProbeResult& probe,
                       const char* module_id) noexcept {
    const auto located = locate_top_level_dds(bytes, size);
    if (!located.ok) {
        return module_support::reject(probe, module_id, located.diagnostic);
    }

    auto confirmed_probe = probe;
    confirmed_probe.content_confirmed = true;
    confirmed_probe.evidence = "DATA_CONFIRMED";

    auto detail = dds_detail(located.document);
    if (located.descriptor_wrapped) detail += " carrier=texture-descriptor+dds";
    if (located.trailing_padding != 0U) {
        detail += " zeroPadding=" + std::to_string(located.trailing_padding);
    }

    auto out = structural_pipeline(confirmed_probe, module_id, detail);
    out.inspection.format = "DDS";
    out.inspection.root = dds_inspection_node(
        located.document, "dds", "DDS", located.source_offset);
    out.inspection.root.kind = InspectionKind::Document;
    if (located.descriptor_wrapped) {
        out.inspection.root.properties.push_back({
            "Carrier", "0x70-byte DMC texture descriptor",
            EvidenceLevel::StructuralConfirmed});
        out.inspection.root.properties.push_back({
            "DDSOffset", std::to_string(located.source_offset),
            EvidenceLevel::StructuralConfirmed});
    }
    if (located.trailing_padding != 0U) {
        out.inspection.root.properties.push_back({
            "TrailingZeroPadding", std::to_string(located.trailing_padding),
            EvidenceLevel::StructuralConfirmed});
    }

    const auto preview = formats::dds::decode_preview(located.bytes, located.document);
    if (preview.ok) {
        out.image_preview = std::move(preview.image);
        out.modules.push_back({"formats.dds.base-mip-preview", true});
    } else {
        out.modules.push_back({"formats.dds.base-mip-preview", false});
        if (!out.detail.empty()) out.detail += "\n";
        out.detail += "Image preview unavailable: " + preview.diagnostic;
    }
    return out;
}

PipelineResult run_ptx(std::string_view,
                       const std::uint8_t* bytes,
                       std::size_t size,
                       const ProbeResult& probe,
                       const char* module_id) noexcept {
    const BinaryReader reader(bytes, size);
    if (!reader.range(0U, kPtxHeaderBytes)) {
        return module_support::reject(
            probe, module_id,
            "PTX rejected: resource is shorter than the 0x800-byte bundle header");
    }

    std::uint32_t count = 0U;
    if (!reader.read_le(0U, &count) || count == 0U || count > kMaxTextureCount ||
        count > (kPtxHeaderBytes - 4U) / 4U) {
        return module_support::reject(probe, module_id,
                                      "PTX rejected: texture count is invalid");
    }

    InspectionNode textures;
    textures.id = "textures";
    textures.title = "Textures";
    textures.kind = InspectionKind::Collection;

    std::vector<ChildResource> child_resources;
    try {
        child_resources.reserve(count);
    } catch (...) {
        return module_support::reject(probe, module_id,
                                      "PTX rejected: child-resource allocation failed");
    }

    std::size_t descriptor = kPtxHeaderBytes;
    std::uint64_t total_dds_bytes = 0U;
    std::uint64_t gallery_preview_pixels = 0U;
    std::uint32_t dxt1 = 0U;
    std::uint32_t dxt5 = 0U;
    std::uint32_t previewed = 0U;

    for (std::uint32_t index = 0U; index < count; ++index) {
        std::uint32_t sector_span = 0U;
        if (!reader.read_le(4U + static_cast<std::size_t>(index) * 4U, &sector_span)) {
            return module_support::reject(
                probe, module_id, "PTX rejected: sector-span table is truncated");
        }

        const bool final = index + 1U == count;
        std::size_t bounded_end = size;
        if (!final || sector_span != 0U) {
            if (sector_span == 0U ||
                sector_span > std::numeric_limits<std::size_t>::max() / kSectorBytes) {
                return module_support::reject(probe, module_id,
                                              "PTX rejected: invalid sector span");
            }
            const auto span_bytes = static_cast<std::size_t>(sector_span) * kSectorBytes;
            if (descriptor > size || span_bytes > size - descriptor) {
                return module_support::reject(
                    probe, module_id, "PTX rejected: sector span leaves resource bounds");
            }
            bounded_end = descriptor + span_bytes;
            if (final && bounded_end != size) {
                return module_support::reject(
                    probe, module_id,
                    "PTX rejected: final sector span does not terminate at EOF");
            }
        }

        if (!reader.range(descriptor, kPtxDescriptorBytes)) {
            return module_support::reject(probe, module_id,
                                          "PTX rejected: descriptor is truncated");
        }

        const std::size_t dds_offset = descriptor + kPtxDescriptorBytes;
        if (dds_offset > bounded_end) {
            return module_support::reject(
                probe, module_id,
                "PTX rejected: descriptor DDS offset leaves bounded span");
        }

        const auto dds_span = std::span<const std::uint8_t>{
            bytes + dds_offset, bounded_end - dds_offset};
        const auto dds = formats::dds::parse(dds_span);
        if (!dds.ok) {
            return module_support::reject(
                probe, module_id,
                "PTX rejected: descriptor is not followed by a valid DXT1/DXT5 DDS");
        }

        std::uint32_t descriptor_payload = 0U;
        std::uint32_t descriptor_dds_size = 0U;
        if (!reader.read_le(descriptor + 0x38U, &descriptor_payload) ||
            !reader.read_le(descriptor + 0x64U, &descriptor_dds_size) ||
            descriptor_payload != dds.document.payload_size ||
            descriptor_dds_size != dds.document.total_size) {
            return module_support::reject(
                probe, module_id,
                "PTX rejected: descriptor DDS sizes disagree with mip payload");
        }

        const std::size_t dds_end = dds_offset + dds.document.total_size;
        if (dds_end > bounded_end) {
            return module_support::reject(probe, module_id,
                                          "PTX rejected: DDS escapes its descriptor span");
        }
        if (final && sector_span == 0U) {
            if (dds_end != size) {
                return module_support::reject(
                    probe, module_id,
                    "PTX rejected: zero-span final DDS does not end at EOF");
            }
        } else if (!module_support::zero_range(reader, dds_end, bounded_end)) {
            return module_support::reject(
                probe, module_id,
                "PTX rejected: alignment padding contains non-zero data");
        }

        total_dds_bytes += dds.document.total_size;
        if (dds.document.compression == formats::dds::Compression::Dxt1) {
            ++dxt1;
        } else {
            ++dxt5;
        }

        auto child_inspection = dds_inspection_node(
            dds.document,
            "texture-" + std::to_string(index),
            "Texture " + std::to_string(index),
            dds_offset);
        child_inspection.properties.push_back({"DescriptorOffset", std::to_string(descriptor),
                                               EvidenceLevel::StructuralConfirmed});
        child_inspection.properties.push_back({"SectorSpan", std::to_string(sector_span),
                                               EvidenceLevel::StructuralConfirmed});
        textures.children.push_back(child_inspection);

        ChildResource child;
        child.id = "dds-" + std::to_string(index);
        child.title = "DDS " + std::to_string(index);
        child.suggested_filename = "texture_" + std::to_string(index) + ".dds";
        child.source_span = SourceSpan{dds_offset, dds.document.total_size};
        child.probe = child_dds_probe();
        child.capabilities = capability(ResourceCapability::Inspection) |
                             ResourceCapability::ImagePreview;
        child.inspection.format = "DDS";
        child.inspection.root = child_inspection;
        child.inspection.root.kind = InspectionKind::Document;
        child.detail = dds_detail(dds.document);
        child.trace = "[OK] formats.dds.child-validation";

        const std::uint64_t pixels =
            static_cast<std::uint64_t>(dds.document.width) * dds.document.height;
        if (pixels <= kMaxPtxGalleryPreviewPixels - gallery_preview_pixels) {
            const auto preview = formats::dds::decode_preview(
                std::span<const std::uint8_t>{bytes + dds_offset,
                                              dds.document.total_size},
                dds.document);
            if (preview.ok) {
                child.image_preview = std::move(preview.image);
                gallery_preview_pixels += pixels;
                ++previewed;
            } else {
                child.detail += "\nImage preview unavailable: " + preview.diagnostic;
            }
        } else {
            child.detail += "\nImage preview omitted by PTX gallery memory budget";
        }

        child_resources.push_back(std::move(child));
        if (!final) descriptor = bounded_end;
    }

    std::ostringstream detail;
    detail << "PTX texture bundle | textures=" << count
           << " dxt1=" << dxt1 << " dxt5=" << dxt5
           << " ddsBytes=" << total_dds_bytes
           << " galleryPreviews=" << previewed;
    auto out = structural_pipeline(probe, module_id, detail.str());
    out.modules.insert(out.modules.begin() + 3,
                       {"formats.dds.child-validation", true});

    out.inspection.format = "PTX";
    out.inspection.root.id = "ptx";
    out.inspection.root.title = "PTX";
    out.inspection.root.kind = InspectionKind::Document;
    out.inspection.root.source_span = SourceSpan{0U, size};
    out.inspection.root.properties.push_back({"TextureCount", std::to_string(count),
                                              EvidenceLevel::StructuralConfirmed});
    out.inspection.root.properties.push_back({"DDSBytes", std::to_string(total_dds_bytes),
                                              EvidenceLevel::StructuralConfirmed});
    out.inspection.root.children.push_back(std::move(textures));
    out.children = std::move(child_resources);
    return out;
}

PipelineResult run_dds_module(const NativeModule& module,
                              std::string_view filename,
                              const std::uint8_t* bytes,
                              std::size_t size,
                              const ProbeResult& probe) noexcept {
    return run_dds(filename, bytes, size, probe, module.id);
}

PipelineResult run_ptx_module(const NativeModule& module,
                              std::string_view filename,
                              const std::uint8_t* bytes,
                              std::size_t size,
                              const ProbeResult& probe) noexcept {
    return run_ptx(filename, bytes, size, probe, module.id);
}

}  // namespace

NativeModule dds_module() noexcept {
    const auto caps = capability(ResourceCapability::Inspection) |
        ResourceCapability::ImagePreview;
    return {"formats.dds.dmc3-reader", "DDS", Format::Dds,
            ModuleKind::Structural, false, run_dds_module, caps};
}

NativeModule ptx_module() noexcept {
    const auto caps = capability(ResourceCapability::Inspection) |
        ResourceCapability::ChildResources;
    return {"formats.ptx.bundle-reader", "PTX", Format::Ptx,
            ModuleKind::Structural, false, run_ptx_module, caps};
}

}  // namespace dmcresource
