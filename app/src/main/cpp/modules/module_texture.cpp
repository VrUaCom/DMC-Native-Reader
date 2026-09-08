#include "dmcresource/native_module.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

#include "dmc_rengine/profiles/dmc3/texture_slot_framing.hpp"
#include "dmcresource/binary_reader.h"
#include "dmcresource/child_resource.h"
#include "dmcresource/formats/dds.h"
#include "dmcresource/module_support.h"

namespace dmcresource {
namespace {

constexpr std::uint64_t kMaxPtxGalleryPreviewPixels = 4ULL * 1024ULL * 1024ULL;

using RengineFramingDocument =
    dmc::rengine::profiles::dmc3::TextureSlotFramingDocument;
using RengineFramingKind =
    dmc::rengine::profiles::dmc3::TextureSlotFramingKind;
using RengineFramingParser =
    dmc::rengine::profiles::dmc3::TextureSlotFramingParser;
using RengineTextureEntry =
    dmc::rengine::profiles::dmc3::TextureSlotEntry;

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

std::span<const std::byte> as_bytes(const std::uint8_t* bytes,
                                    std::size_t size) noexcept {
    if (bytes == nullptr) return {};
    return std::as_bytes(std::span<const std::uint8_t>{bytes, size});
}

formats::dds::Document dds_document_from_frame(
    const RengineTextureEntry& frame) noexcept {
    formats::dds::Document out;
    out.width = frame.width;
    out.height = frame.height;
    out.mip_count = frame.mip_map_count;
    out.payload_size = frame.dds_payload_size;
    out.total_size = frame.dds_size;
    out.compression = frame.compression ==
            dmc::rengine::profiles::dmc3::TextureCompressionKind::dxt1
        ? formats::dds::Compression::Dxt1
        : formats::dds::Compression::Dxt5;
    return out;
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

    // Standalone DDS is handled by the reusable DDS reader/preview module.
    // Partial mip chains are valid here when the declared chain is bounded by
    // the supplied bytes. Only zero trailer padding is tolerated.
    const auto direct_span = std::span<const std::uint8_t>{bytes, size};
    const auto direct = formats::dds::parse(direct_span);
    if (direct.ok) {
        const auto end = static_cast<std::size_t>(direct.document.total_size);
        const BinaryReader reader(bytes, size);
        if (!module_support::zero_range(reader, end, size)) {
            out.diagnostic =
                "DDS rejected: non-zero bytes follow the bounded DDS payload";
            return out;
        }
        out.ok = true;
        out.document = direct.document;
        out.bytes = std::span<const std::uint8_t>{bytes, end};
        out.trailing_padding = size - end;
        return out;
    }

    // DMC descriptor+DDS framing is canonical DMC Rengine territory. Native
    // Reader does not reinterpret +0x38/+0x64 or any other descriptor fields.
    // The vendored parser is copied text-identically from dmc-rengine-cpp and
    // returns a typed TextureSlotEntry only after the full framing contract is
    // accepted.
    const auto framing = RengineFramingParser::parse(as_bytes(bytes, size));
    if (!framing.ok()) {
        out.diagnostic = "DDS rejected: ";
        if (!framing.detail.empty()) {
            out.diagnostic.append(framing.detail.data(), framing.detail.size());
        } else if (!direct.diagnostic.empty()) {
            out.diagnostic += direct.diagnostic;
        } else {
            out.diagnostic += "no valid direct or DMC descriptor-wrapped DDS";
        }
        return out;
    }
    if (framing.document.kind != RengineFramingKind::wrapped_dds ||
        framing.document.textures.size() != 1U) {
        out.diagnostic =
            "DDS rejected: resource is a DMC texture bundle rather than a single DDS";
        return out;
    }

    const auto& frame = framing.document.textures.front();
    if (frame.dds_offset > size || frame.dds_size > size - frame.dds_offset) {
        out.diagnostic = "DDS rejected: canonical texture frame escaped input bounds";
        return out;
    }

    out.ok = true;
    out.document = dds_document_from_frame(frame);
    out.bytes = std::span<const std::uint8_t>{
        bytes + static_cast<std::size_t>(frame.dds_offset),
        static_cast<std::size_t>(frame.dds_size)};
    out.source_offset = static_cast<std::size_t>(frame.dds_offset);
    out.descriptor_wrapped = true;
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
    if (located.descriptor_wrapped) {
        detail += " carrier=rengine-texture-slot-frame";
    }
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
            "Carrier", "canonical DMC texture-slot frame",
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

    const auto preview = formats::dds::decode_preview(
        located.bytes, located.document);
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
    if (bytes == nullptr) {
        return module_support::reject(probe, module_id, "PTX rejected: null input");
    }

    // PTX physical framing, descriptor validation, sector spans and alignment
    // are all parsed once by the canonical dmc-rengine-cpp profile parser.
    // Native Reader only projects the typed frame entries into generic child
    // resources and image previews.
    const auto framing = RengineFramingParser::parse(as_bytes(bytes, size));
    if (!framing.ok()) {
        std::string diagnostic = "PTX rejected: ";
        if (!framing.detail.empty()) {
            diagnostic.append(framing.detail.data(), framing.detail.size());
        } else {
            diagnostic += "canonical texture-slot framing parser rejected input";
        }
        return module_support::reject(probe, module_id, std::move(diagnostic));
    }
    if (framing.document.kind != RengineFramingKind::texture_bundle) {
        return module_support::reject(
            probe, module_id,
            "PTX rejected: canonical texture framing resolved a single wrapped DDS");
    }

    const RengineFramingDocument& document = framing.document;
    InspectionNode textures;
    textures.id = "textures";
    textures.title = "Textures";
    textures.kind = InspectionKind::Collection;

    std::vector<ChildResource> child_resources;
    try {
        child_resources.reserve(document.textures.size());
        textures.children.reserve(document.textures.size());
    } catch (...) {
        return module_support::reject(
            probe, module_id, "PTX rejected: child-resource allocation failed");
    }

    std::uint64_t total_dds_bytes = 0U;
    std::uint64_t gallery_preview_pixels = 0U;
    std::uint32_t dxt1 = 0U;
    std::uint32_t dxt5 = 0U;
    std::uint32_t previewed = 0U;

    for (const auto& frame : document.textures) {
        if (frame.dds_offset > size || frame.dds_size > size - frame.dds_offset) {
            return module_support::reject(
                probe, module_id,
                "PTX rejected: canonical texture frame escaped input bounds");
        }

        const auto dds = dds_document_from_frame(frame);
        const auto dds_span = std::span<const std::uint8_t>{
            bytes + static_cast<std::size_t>(frame.dds_offset),
            static_cast<std::size_t>(frame.dds_size)};

        total_dds_bytes += frame.dds_size;
        if (dds.compression == formats::dds::Compression::Dxt1) {
            ++dxt1;
        } else {
            ++dxt5;
        }

        const auto index = frame.texture_index;
        auto child_inspection = dds_inspection_node(
            dds,
            "texture-" + std::to_string(index),
            "Texture " + std::to_string(index),
            static_cast<std::size_t>(frame.dds_offset));
        child_inspection.properties.push_back({
            "DescriptorOffset", std::to_string(frame.descriptor_offset),
            EvidenceLevel::StructuralConfirmed});
        child_inspection.properties.push_back({
            "SectorSpan", std::to_string(frame.sector_span),
            EvidenceLevel::StructuralConfirmed});
        textures.children.push_back(child_inspection);

        ChildResource child;
        child.id = "dds-" + std::to_string(index);
        child.title = "DDS " + std::to_string(index);
        child.suggested_filename = "texture_" + std::to_string(index) + ".dds";
        child.source_span = SourceSpan{
            static_cast<std::size_t>(frame.dds_offset),
            static_cast<std::size_t>(frame.dds_size)};
        child.probe = child_dds_probe();
        child.capabilities = capability(ResourceCapability::Inspection) |
                             ResourceCapability::ImagePreview;
        child.inspection.format = "DDS";
        child.inspection.root = child_inspection;
        child.inspection.root.kind = InspectionKind::Document;
        child.detail = dds_detail(dds);
        child.trace = "[OK] dmc-rengine.texture-slot-framing\n"
                      "[OK] formats.dds.base-mip-preview";

        const std::uint64_t pixels =
            static_cast<std::uint64_t>(dds.width) * dds.height;
        if (pixels <= kMaxPtxGalleryPreviewPixels - gallery_preview_pixels) {
            const auto preview = formats::dds::decode_preview(dds_span, dds);
            if (preview.ok) {
                child.image_preview = std::move(preview.image);
                gallery_preview_pixels += pixels;
                ++previewed;
            } else {
                child.detail += "\nImage preview unavailable: " + preview.diagnostic;
                child.trace = "[OK] dmc-rengine.texture-slot-framing\n"
                              "[FAIL] formats.dds.base-mip-preview";
            }
        } else {
            child.detail += "\nImage preview omitted by PTX gallery memory budget";
        }

        child_resources.push_back(std::move(child));
    }

    std::ostringstream detail;
    detail << "PTX texture bundle | textures=" << document.textures.size()
           << " dxt1=" << dxt1 << " dxt5=" << dxt5
           << " ddsBytes=" << total_dds_bytes
           << " galleryPreviews=" << previewed;
    auto out = structural_pipeline(probe, module_id, detail.str());
    out.modules.insert(out.modules.begin() + 3,
                       {"dmc-rengine.texture-slot-framing", true});

    out.inspection.format = "PTX";
    out.inspection.root.id = "ptx";
    out.inspection.root.title = "PTX";
    out.inspection.root.kind = InspectionKind::Document;
    out.inspection.root.source_span = SourceSpan{0U, size};
    out.inspection.root.properties.push_back({
        "TextureCount", std::to_string(document.textures.size()),
        EvidenceLevel::StructuralConfirmed});
    out.inspection.root.properties.push_back({
        "DDSBytes", std::to_string(total_dds_bytes),
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
