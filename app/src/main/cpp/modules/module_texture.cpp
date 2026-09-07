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
#include "dmcresource/formats/dds.h"
#include "dmcresource/module_support.h"

namespace dmcresource {
namespace {

constexpr std::size_t kPtxHeaderBytes = 0x800U;
constexpr std::size_t kPtxDescriptorBytes = 0x70U;
constexpr std::size_t kSectorBytes = 0x800U;
constexpr std::uint32_t kMaxTextureCount = 4096U;

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

PipelineResult run_dds(std::string_view,
                       const std::uint8_t* bytes,
                       std::size_t size,
                       const ProbeResult& probe,
                       const char* module_id) noexcept {
    if (bytes == nullptr) {
        return module_support::reject(probe, module_id, "DDS rejected: null input");
    }

    const auto span = std::span<const std::uint8_t>{bytes, size};
    const auto parsed = formats::dds::parse(span);
    if (!parsed.ok || parsed.document.total_size != size) {
        return module_support::reject(
            probe, module_id,
            "DDS rejected: expected a bounded complete DXT1/DXT5 full mip chain");
    }

    std::ostringstream detail;
    detail << "DDS " << formats::dds::compression_name(parsed.document.compression)
           << " " << parsed.document.width << "x" << parsed.document.height
           << " mips=" << parsed.document.mip_count
           << " payload=" << parsed.document.payload_size;
    auto out = structural_pipeline(probe, module_id, detail.str());
    out.inspection.format = "DDS";
    out.inspection.root = dds_inspection_node(parsed.document, "dds", "DDS", 0U);
    out.inspection.root.kind = InspectionKind::Document;

    const auto preview = formats::dds::decode_preview(span, parsed.document);
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

    std::size_t descriptor = kPtxHeaderBytes;
    std::uint64_t total_dds_bytes = 0U;
    std::uint32_t dxt1 = 0U;
    std::uint32_t dxt5 = 0U;

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

        const auto dds = formats::dds::parse(std::span<const std::uint8_t>{
            bytes + dds_offset, bounded_end - dds_offset});
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

        auto child = dds_inspection_node(
            dds.document,
            "texture-" + std::to_string(index),
            "Texture " + std::to_string(index),
            dds_offset);
        child.properties.push_back({"DescriptorOffset", std::to_string(descriptor),
                                    EvidenceLevel::StructuralConfirmed});
        child.properties.push_back({"SectorSpan", std::to_string(sector_span),
                                    EvidenceLevel::StructuralConfirmed});
        textures.children.push_back(std::move(child));

        if (!final) descriptor = bounded_end;
    }

    std::ostringstream detail;
    detail << "PTX texture bundle | textures=" << count
           << " dxt1=" << dxt1 << " dxt5=" << dxt5
           << " ddsBytes=" << total_dds_bytes;
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
