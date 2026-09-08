#include "dmcresource/native_module.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

#include "dmc_rengine/codecs/dds_bc.hpp"
#include "dmc_rengine/profiles/dmc3/texture_slot_framing.hpp"
#include "dmc_rengine/spider/native_executor.hpp"
#include "dmcresource/child_resource.h"
#include "dmcresource/module_support.h"

namespace dmcresource {
namespace {

namespace dds_bc = dmc::rengine::codecs::dds_bc;
namespace dmc3 = dmc::rengine::profiles::dmc3;
namespace spider = dmc::rengine::spider;

constexpr std::uint64_t kMaxPtxGalleryPreviewPixels =
    4ULL * 1024ULL * 1024ULL;

constexpr spider::NativeOperationId kTextureFramePtx = 1U;
constexpr spider::NativeOperationId kTextureProject = 2U;

[[nodiscard]] std::span<const std::byte> as_bytes(
    const std::uint8_t* bytes,
    std::size_t size) noexcept {
    return std::as_bytes(std::span<const std::uint8_t>{bytes, size});
}

[[nodiscard]] std::span<const std::byte> entry_dds_span(
    std::span<const std::byte> source,
    const dmc3::TextureSlotEntry& entry) noexcept {
    if (entry.dds_offset > source.size()) return {};
    const auto offset = static_cast<std::size_t>(entry.dds_offset);
    const auto dds_size = static_cast<std::size_t>(entry.dds_size);
    if (dds_size > source.size() - offset) return {};
    return source.subspan(offset, dds_size);
}

[[nodiscard]] ImagePreview to_image_preview(dds_bc::RgbaImage image) {
    ImagePreview out;
    out.width = image.width;
    out.height = image.height;
    out.rgba8 = std::move(image.rgba8);
    return out;
}

InspectionNode dds_inspection_node(
    const dds_bc::Document& dds,
    std::string id,
    std::string title,
    std::uint64_t offset) {
    InspectionNode node;
    node.id = std::move(id);
    node.title = std::move(title);
    node.kind = InspectionKind::Texture;
    node.source_span = SourceSpan{offset, dds.total_size};
    node.properties.push_back({
        "Compression", dds_bc::compression_name(dds.compression),
        EvidenceLevel::DataConfirmed});
    node.properties.push_back({
        "Width", std::to_string(dds.width),
        EvidenceLevel::StructuralConfirmed});
    node.properties.push_back({
        "Height", std::to_string(dds.height),
        EvidenceLevel::StructuralConfirmed});
    node.properties.push_back({
        "MipCount", std::to_string(dds.mip_count),
        EvidenceLevel::StructuralConfirmed});
    node.properties.push_back({
        "PayloadBytes", std::to_string(dds.payload_size),
        EvidenceLevel::StructuralConfirmed});
    return node;
}

std::string dds_detail(const dds_bc::Document& dds) {
    std::ostringstream detail;
    detail << "DDS " << dds_bc::compression_name(dds.compression)
           << " " << dds.width << "x" << dds.height
           << " mips=" << dds.mip_count
           << " payload=" << dds.payload_size;
    return detail.str();
}

ProbeResult child_dds_probe() noexcept {
    return {Format::Dds, true, true, "DDS", "texture", "recognized",
            "DATA_CONFIRMED", "image/vnd-ms.dds"};
}

[[nodiscard]] dds_bc::ParseResult parse_exact_dds(
    std::span<const std::byte> bytes) noexcept {
    auto parsed = dds_bc::parse(bytes);
    if (parsed.ok() && parsed.document.total_size != bytes.size()) {
        return dds_bc::ParseResult{
            .status = dds_bc::Status::payload_out_of_bounds,
            .document = {},
            .detail = "DDS has trailing bytes outside its bounded image extent",
        };
    }
    return parsed;
}

[[nodiscard]] bool attach_preview(
    PipelineResult* out,
    std::span<const std::byte> bytes,
    const dds_bc::Document& dds) {
    const auto decoded = dds_bc::decode_base_mip_rgba8(bytes, dds);
    if (!decoded.ok) {
        out->modules.push_back({"formats.dds.base-mip-preview", false});
        if (!out->detail.empty()) out->detail += "\n";
        out->detail += "Image preview unavailable: ";
        out->detail += decoded.detail;
        return false;
    }
    out->image_preview = to_image_preview(std::move(decoded.image));
    out->modules.push_back({"formats.dds.base-mip-preview", true});
    return true;
}

PipelineResult run_plain_dds(
    std::span<const std::byte> source,
    const dds_bc::ParseResult& parsed,
    const ProbeResult& probe,
    const char* module_id) {
    auto out = structural_pipeline(
        probe, module_id, dds_detail(parsed.document));
    out.inspection.format = "DDS";
    out.inspection.root = dds_inspection_node(
        parsed.document, "dds", "DDS", 0U);
    out.inspection.root.kind = InspectionKind::Document;
    static_cast<void>(attach_preview(&out, source, parsed.document));
    return out;
}

PipelineResult run_wrapped_dds(
    std::span<const std::byte> source,
    const dmc3::TextureSlotFramingResult& framing,
    const ProbeResult& probe,
    const char* module_id) {
    if (framing.document.textures.size() != 1U) {
        return module_support::reject(
            probe, module_id,
            "Wrapped DDS rejected: canonical framing did not expose one texture");
    }

    const auto& entry = framing.document.textures.front();
    const auto dds_bytes = entry_dds_span(source, entry);
    const auto parsed = parse_exact_dds(dds_bytes);
    if (!parsed.ok()) {
        return module_support::reject(
            probe, module_id,
            "Wrapped DDS rejected: canonical framing child is not a bounded DXT DDS");
    }

    auto out = structural_pipeline(
        probe, module_id, "DMC3 descriptor-wrapped " + dds_detail(parsed.document));
    out.modules.push_back({"profiles.dmc3.texture-slot-framing", true});
    out.inspection.format = "DDS";
    out.inspection.root.id = "wrapped-dds";
    out.inspection.root.title = "Wrapped DDS";
    out.inspection.root.kind = InspectionKind::Document;
    out.inspection.root.source_span = SourceSpan{0U, source.size()};

    auto texture = dds_inspection_node(
        parsed.document, "texture-0", "Texture 0", entry.dds_offset);
    texture.properties.push_back({
        "DescriptorOffset", std::to_string(entry.descriptor_offset),
        EvidenceLevel::StructuralConfirmed});
    texture.properties.push_back({
        "SecondaryWidth", std::to_string(entry.secondary_width),
        EvidenceLevel::StructuralConfirmed});
    texture.properties.push_back({
        "SecondaryHeight", std::to_string(entry.secondary_height),
        EvidenceLevel::StructuralConfirmed});
    out.inspection.root.children.push_back(std::move(texture));

    static_cast<void>(attach_preview(&out, dds_bytes, parsed.document));
    return out;
}

PipelineResult run_direct_dds(
    const std::uint8_t* bytes,
    std::size_t size,
    const ProbeResult& probe,
    const char* module_id) noexcept {
    if (bytes == nullptr) {
        return module_support::reject(
            probe, module_id, "DDS rejected: null input");
    }

    const auto source = as_bytes(bytes, size);
    const auto parsed = parse_exact_dds(source);
    if (parsed.ok()) {
        return run_plain_dds(source, parsed, probe, module_id);
    }

    const auto framing = dmc3::TextureSlotFramingParser::parse(source);
    if (framing.ok() && framing.document.kind ==
            dmc3::TextureSlotFramingKind::wrapped_dds) {
        return run_wrapped_dds(source, framing, probe, module_id);
    }

    return module_support::reject(
        probe, module_id,
        "DDS rejected: neither a bounded standalone DXT DDS nor canonical descriptor-wrapped DDS");
}

PipelineResult run_framed_ptx(
    std::span<const std::byte> source,
    const dmc3::TextureSlotFramingResult& framing,
    const ProbeResult& probe,
    const char* module_id) noexcept {
    InspectionNode textures;
    textures.id = "textures";
    textures.title = "Textures";
    textures.kind = InspectionKind::Collection;

    std::vector<ChildResource> child_resources;
    try {
        child_resources.reserve(framing.document.textures.size());
    } catch (...) {
        return module_support::reject(
            probe, module_id,
            "PTX rejected: child-resource allocation failed");
    }

    std::uint64_t total_dds_bytes = 0U;
    std::uint64_t gallery_preview_pixels = 0U;
    std::uint32_t dxt1 = 0U;
    std::uint32_t dxt5 = 0U;
    std::uint32_t previewed = 0U;

    for (const auto& entry : framing.document.textures) {
        const auto dds_bytes = entry_dds_span(source, entry);
        const auto dds = parse_exact_dds(dds_bytes);
        if (!dds.ok()) {
            return module_support::reject(
                probe, module_id,
                "PTX rejected: framed child failed portable DDS validation");
        }

        total_dds_bytes += dds.document.total_size;
        if (dds.document.compression == dds_bc::Compression::dxt1) {
            ++dxt1;
        } else {
            ++dxt5;
        }

        auto child_inspection = dds_inspection_node(
            dds.document,
            "texture-" + std::to_string(entry.texture_index),
            "Texture " + std::to_string(entry.texture_index),
            entry.dds_offset);
        child_inspection.properties.push_back({
            "DescriptorOffset", std::to_string(entry.descriptor_offset),
            EvidenceLevel::StructuralConfirmed});
        child_inspection.properties.push_back({
            "SectorSpan", std::to_string(entry.sector_span),
            EvidenceLevel::StructuralConfirmed});
        child_inspection.properties.push_back({
            "SecondaryWidth", std::to_string(entry.secondary_width),
            EvidenceLevel::StructuralConfirmed});
        child_inspection.properties.push_back({
            "SecondaryHeight", std::to_string(entry.secondary_height),
            EvidenceLevel::StructuralConfirmed});
        textures.children.push_back(child_inspection);

        ChildResource child;
        child.id = "dds-" + std::to_string(entry.texture_index);
        child.title = "DDS " + std::to_string(entry.texture_index);
        child.suggested_filename =
            "texture_" + std::to_string(entry.texture_index) + ".dds";
        child.source_span = SourceSpan{entry.dds_offset, entry.dds_size};
        child.probe = child_dds_probe();
        child.capabilities = capability(ResourceCapability::Inspection) |
                             ResourceCapability::ImagePreview;
        child.inspection.format = "DDS";
        child.inspection.root = child_inspection;
        child.inspection.root.kind = InspectionKind::Document;
        child.detail = dds_detail(dds.document);
        child.trace = "[OK] profiles.dmc3.texture-slot-framing\n"
                      "[OK] formats.dds.child-validation";

        const auto pixels =
            static_cast<std::uint64_t>(dds.document.width) *
            static_cast<std::uint64_t>(dds.document.height);
        if (pixels <= kMaxPtxGalleryPreviewPixels &&
            gallery_preview_pixels <= kMaxPtxGalleryPreviewPixels - pixels) {
            const auto decoded = dds_bc::decode_base_mip_rgba8(
                dds_bytes, dds.document);
            if (decoded.ok) {
                child.image_preview = to_image_preview(std::move(decoded.image));
                gallery_preview_pixels += pixels;
                ++previewed;
            } else {
                child.detail += "\nImage preview unavailable: ";
                child.detail += decoded.detail;
            }
        } else {
            child.detail += "\nImage preview omitted by PTX gallery memory budget";
        }

        child_resources.push_back(std::move(child));
    }

    std::ostringstream detail;
    detail << "PTX texture bundle | textures="
           << framing.document.textures.size()
           << " dxt1=" << dxt1
           << " dxt5=" << dxt5
           << " ddsBytes=" << total_dds_bytes
           << " galleryPreviews=" << previewed;

    auto out = structural_pipeline(probe, module_id, detail.str());
    out.modules.push_back({"profiles.dmc3.texture-slot-framing", true});
    out.modules.push_back({"formats.dds.child-validation", true});
    out.inspection.format = "PTX";
    out.inspection.root.id = "ptx";
    out.inspection.root.title = "PTX";
    out.inspection.root.kind = InspectionKind::Document;
    out.inspection.root.source_span = SourceSpan{0U, source.size()};
    out.inspection.root.properties.push_back({
        "TextureCount", std::to_string(framing.document.textures.size()),
        EvidenceLevel::StructuralConfirmed});
    out.inspection.root.properties.push_back({
        "DDSBytes", std::to_string(total_dds_bytes),
        EvidenceLevel::StructuralConfirmed});
    out.inspection.root.children.push_back(std::move(textures));
    out.children = std::move(child_resources);
    return out;
}

struct TextureExecutionState final {
    const std::uint8_t* bytes{};
    std::size_t size{};
    const ProbeResult* probe{};
    const char* module_id{};
    dmc3::TextureSlotFramingResult framing{};
    PipelineResult result{};
};

bool frame_ptx_operation(void* raw, std::uint32_t) noexcept {
    auto* state = static_cast<TextureExecutionState*>(raw);
    if (state == nullptr || state->probe == nullptr || state->module_id == nullptr) {
        return false;
    }
    if (state->bytes == nullptr) {
        state->result = module_support::reject(
            *state->probe, state->module_id, "PTX rejected: null input");
        return false;
    }

    const auto source = as_bytes(state->bytes, state->size);
    state->framing = dmc3::TextureSlotFramingParser::parse(source);
    if (!state->framing.ok() || state->framing.document.kind !=
            dmc3::TextureSlotFramingKind::texture_bundle) {
        std::string detail = "PTX rejected by canonical texture-slot framing";
        if (!state->framing.detail.empty()) {
            detail += ": ";
            detail += state->framing.detail;
        }
        state->result = module_support::reject(
            *state->probe, state->module_id, std::move(detail));
        return false;
    }
    return true;
}

bool project_texture_operation(void* raw, std::uint32_t) noexcept {
    auto* state = static_cast<TextureExecutionState*>(raw);
    if (state == nullptr || state->probe == nullptr || state->module_id == nullptr) {
        return false;
    }

    if (state->probe->format == Format::Dds) {
        state->result = run_direct_dds(
            state->bytes, state->size, *state->probe, state->module_id);
        return state->result.accepted;
    }

    if (state->probe->format == Format::Ptx) {
        if (state->bytes == nullptr || !state->framing.ok() ||
            state->framing.document.kind != dmc3::TextureSlotFramingKind::texture_bundle) {
            state->result = module_support::reject(
                *state->probe, state->module_id,
                "PTX rejected: Spider framing dependency is unavailable");
            return false;
        }
        state->result = run_framed_ptx(
            as_bytes(state->bytes, state->size), state->framing,
            *state->probe, state->module_id);
        return state->result.accepted;
    }

    state->result = module_support::reject(
        *state->probe, state->module_id,
        "Texture pipeline rejected: unsupported route");
    return false;
}

const spider::NativePlan& direct_dds_plan() {
    static const spider::NativePlan plan = [] {
        spider::NativePlan out;
        out.instructions.push_back(spider::NativeInstruction{
            .operation = kTextureProject,
            .operand = 0U,
            .dependency_begin = 0U,
            .dependency_count = 0U,
            .domain = spider::ExecutionDomain::cpu,
        });
        return out;
    }();
    return plan;
}

const spider::NativePlan& ptx_plan() {
    static const spider::NativePlan plan = [] {
        spider::NativePlan out;
        out.dependencies.push_back(0U);
        out.instructions.push_back(spider::NativeInstruction{
            .operation = kTextureFramePtx,
            .operand = 0U,
            .dependency_begin = 0U,
            .dependency_count = 0U,
            .domain = spider::ExecutionDomain::cpu,
        });
        out.instructions.push_back(spider::NativeInstruction{
            .operation = kTextureProject,
            .operand = 0U,
            .dependency_begin = 0U,
            .dependency_count = 1U,
            .domain = spider::ExecutionDomain::cpu,
        });
        return out;
    }();
    return plan;
}

PipelineResult run_texture_module(
    const NativeModule& module,
    std::string_view,
    const std::uint8_t* bytes,
    std::size_t size,
    const ProbeResult& probe) noexcept {
    TextureExecutionState state{
        .bytes = bytes,
        .size = size,
        .probe = &probe,
        .module_id = module.id,
    };

    static const std::array bindings{
        spider::NativeOperationBinding{
            .operation = kTextureFramePtx,
            .execute = &frame_ptx_operation,
        },
        spider::NativeOperationBinding{
            .operation = kTextureProject,
            .execute = &project_texture_operation,
        },
    };

    const spider::NativePlan* plan = nullptr;
    if (module.format == Format::Dds) {
        plan = &direct_dds_plan();
    } else if (module.format == Format::Ptx) {
        plan = &ptx_plan();
    }

    if (plan == nullptr) {
        return module_support::reject(
            probe, module.id, "Texture pipeline rejected: invalid module route");
    }

    const auto report = spider::execute_native_plan(*plan, bindings, &state);
    if (!report.ok()) {
        if (!state.result.detail.empty()) return state.result;
        std::string detail = "Spider texture execution failed: ";
        detail += spider::to_string(report.status);
        return module_support::reject(probe, module.id, std::move(detail));
    }

    state.result.modules.push_back({"spider.native-executor", true});
    return state.result;
}

} // namespace

NativeModule texture_module(Format format) noexcept {
    if (format == Format::Ptx) {
        const auto caps = capability(ResourceCapability::Inspection) |
            ResourceCapability::ChildResources;
        return {"formats.texture.spider-reader", "PTX", Format::Ptx,
                ModuleKind::Structural, false, run_texture_module, caps};
    }

    const auto caps = capability(ResourceCapability::Inspection) |
        ResourceCapability::ImagePreview;
    return {"formats.texture.spider-reader", "DDS", Format::Dds,
            ModuleKind::Structural, false, run_texture_module, caps};
}

} // namespace dmcresource
