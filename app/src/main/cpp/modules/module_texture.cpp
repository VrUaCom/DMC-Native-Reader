#include "dmcresource/native_module.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "dmc_rengine/codecs/dds_bc.hpp"
#include "dmcresource/child_resource.h"
#include "dmcresource/module_support.h"
#include "dmcresource/spider/crusader.h"
#include "dmcresource/texture_set.h"

namespace dmcresource {
namespace {

namespace dds_bc = dmc::rengine::codecs::dds_bc;
namespace crusader = dmcresource::spider::crusader;
namespace textures = dmcresource::texture_set;

constexpr std::uint64_t kMaxPtxGalleryPreviewPixels =
    4ULL * 1024ULL * 1024ULL;

constexpr crusader::OperationId kTextureFramePtx = 1U;
constexpr crusader::OperationId kTextureProject = 2U;

[[nodiscard]] std::span<const std::byte> as_bytes(
    const std::uint8_t* bytes,
    std::size_t size) noexcept {
    if (bytes == nullptr) return {};
    return std::as_bytes(std::span<const std::uint8_t>{bytes, size});
}

InspectionNode dds_inspection_node(
    const textures::Slot& slot,
    std::string id,
    std::string title) {
    InspectionNode node;
    node.id = std::move(id);
    node.title = std::move(title);
    node.kind = InspectionKind::Texture;
    node.source_span = SourceSpan{slot.dds_offset, slot.dds.total_size};
    node.properties.push_back({
        "Compression", dds_bc::compression_name(slot.dds.compression),
        EvidenceLevel::DataConfirmed});
    node.properties.push_back({
        "Width", std::to_string(slot.dds.width),
        EvidenceLevel::StructuralConfirmed});
    node.properties.push_back({
        "Height", std::to_string(slot.dds.height),
        EvidenceLevel::StructuralConfirmed});
    node.properties.push_back({
        "MipCount", std::to_string(slot.dds.mip_count),
        EvidenceLevel::StructuralConfirmed});
    node.properties.push_back({
        "PayloadBytes", std::to_string(slot.dds.payload_size),
        EvidenceLevel::StructuralConfirmed});
    return node;
}

std::string dds_detail(const textures::Slot& slot) {
    std::ostringstream detail;
    detail << "DDS " << dds_bc::compression_name(slot.dds.compression)
           << " " << slot.dds.width << "x" << slot.dds.height
           << " mips=" << slot.dds.mip_count
           << " payload=" << slot.dds.payload_size;
    return detail.str();
}

ProbeResult child_dds_probe() noexcept {
    return {Format::Dds, true, true, "DDS", "texture", "recognized",
            "DATA_CONFIRMED", "image/vnd-ms.dds"};
}

[[nodiscard]] bool attach_preview(
    PipelineResult* out,
    std::span<const std::byte> source,
    const textures::Slot& slot) {
    if (out == nullptr) return false;
    std::string detail;
    if (!textures::decode_base_mip(source, slot, &out->image_preview, &detail)) {
        out->modules.push_back({"formats.dds.base-mip-preview", false});
        if (!out->detail.empty()) out->detail += "\n";
        out->detail += "Image preview unavailable";
        if (!detail.empty()) {
            out->detail += ": ";
            out->detail += detail;
        }
        return false;
    }
    out->modules.push_back({"formats.dds.base-mip-preview", true});
    return true;
}

PipelineResult run_dds_set(
    std::span<const std::byte> source,
    const textures::ParseResult& set,
    const ProbeResult& probe,
    const char* module_id) {
    if (!set.ok() || set.slots.size() != 1U) {
        return module_support::reject(
            probe, module_id,
            set.detail.empty() ? "DDS rejected by TextureSet" : set.detail);
    }

    const auto& slot = set.slots.front();
    if (set.kind == textures::Kind::standalone_dds) {
        auto out = structural_pipeline(probe, module_id, dds_detail(slot));
        out.modules.push_back({"native.texture-set", true});
        out.inspection.format = "DDS";
        out.inspection.root = dds_inspection_node(slot, "dds", "DDS");
        out.inspection.root.kind = InspectionKind::Document;
        static_cast<void>(attach_preview(&out, source, slot));
        return out;
    }

    if (set.kind != textures::Kind::wrapped_dds) {
        return module_support::reject(
            probe, module_id, "DDS rejected: invalid TextureSet kind");
    }

    auto out = structural_pipeline(
        probe, module_id, "DMC3 descriptor-wrapped " + dds_detail(slot));
    out.modules.push_back({"native.texture-set", true});
    out.modules.push_back({"profiles.dmc3.texture-slot-framing", true});
    out.inspection.format = "DDS";
    out.inspection.root.id = "wrapped-dds";
    out.inspection.root.title = "Wrapped DDS";
    out.inspection.root.kind = InspectionKind::Document;
    out.inspection.root.source_span = SourceSpan{0U, source.size()};

    auto texture = dds_inspection_node(slot, "texture-0", "Texture 0");
    texture.properties.push_back({
        "DescriptorOffset", std::to_string(slot.descriptor_offset),
        EvidenceLevel::StructuralConfirmed});
    texture.properties.push_back({
        "SecondaryWidth", std::to_string(slot.secondary_width),
        EvidenceLevel::StructuralConfirmed});
    texture.properties.push_back({
        "SecondaryHeight", std::to_string(slot.secondary_height),
        EvidenceLevel::StructuralConfirmed});
    out.inspection.root.children.push_back(std::move(texture));

    static_cast<void>(attach_preview(&out, source, slot));
    return out;
}

PipelineResult run_ptx_set(
    std::span<const std::byte> source,
    const textures::ParseResult& set,
    const ProbeResult& probe,
    const char* module_id) {
    if (!set.ok() || set.kind != textures::Kind::ptx_bundle) {
        return module_support::reject(
            probe, module_id,
            set.detail.empty() ? "PTX rejected by TextureSet" : set.detail);
    }

    InspectionNode texture_nodes;
    texture_nodes.id = "textures";
    texture_nodes.title = "Textures";
    texture_nodes.kind = InspectionKind::Collection;

    std::vector<ChildResource> child_resources;
    try {
        child_resources.reserve(set.slots.size());
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

    for (const auto& slot : set.slots) {
        total_dds_bytes += slot.dds.total_size;
        if (slot.dds.compression == dds_bc::Compression::dxt1) {
            ++dxt1;
        } else {
            ++dxt5;
        }

        auto child_inspection = dds_inspection_node(
            slot,
            "texture-" + std::to_string(slot.index),
            "Texture " + std::to_string(slot.index));
        child_inspection.properties.push_back({
            "DescriptorOffset", std::to_string(slot.descriptor_offset),
            EvidenceLevel::StructuralConfirmed});
        child_inspection.properties.push_back({
            "SectorSpan", std::to_string(slot.sector_span),
            EvidenceLevel::StructuralConfirmed});
        child_inspection.properties.push_back({
            "SecondaryWidth", std::to_string(slot.secondary_width),
            EvidenceLevel::StructuralConfirmed});
        child_inspection.properties.push_back({
            "SecondaryHeight", std::to_string(slot.secondary_height),
            EvidenceLevel::StructuralConfirmed});
        texture_nodes.children.push_back(child_inspection);

        ChildResource child;
        child.id = "dds-" + std::to_string(slot.index);
        child.title = "DDS " + std::to_string(slot.index);
        child.suggested_filename =
            "texture_" + std::to_string(slot.index) + ".dds";
        child.source_span = SourceSpan{slot.dds_offset, slot.dds_size};
        child.probe = child_dds_probe();
        child.capabilities = capability(ResourceCapability::Inspection) |
                             ResourceCapability::ImagePreview;
        child.inspection.format = "DDS";
        child.inspection.root = child_inspection;
        child.inspection.root.kind = InspectionKind::Document;
        child.detail = dds_detail(slot);
        child.trace = "[OK] native.texture-set\n"
                      "[OK] profiles.dmc3.texture-slot-framing\n"
                      "[OK] formats.dds.child-validation";

        const auto pixels =
            static_cast<std::uint64_t>(slot.dds.width) *
            static_cast<std::uint64_t>(slot.dds.height);
        if (pixels <= kMaxPtxGalleryPreviewPixels &&
            gallery_preview_pixels <= kMaxPtxGalleryPreviewPixels - pixels) {
            std::string decode_detail;
            if (textures::decode_base_mip(
                    source, slot, &child.image_preview, &decode_detail)) {
                gallery_preview_pixels += pixels;
                ++previewed;
            } else {
                child.detail += "\nImage preview unavailable";
                if (!decode_detail.empty()) {
                    child.detail += ": ";
                    child.detail += decode_detail;
                }
            }
        } else {
            child.detail += "\nImage preview omitted by PTX gallery memory budget";
        }

        child_resources.push_back(std::move(child));
    }

    std::ostringstream detail;
    detail << "PTX texture bundle | textures=" << set.slots.size()
           << " dxt1=" << dxt1
           << " dxt5=" << dxt5
           << " ddsBytes=" << total_dds_bytes
           << " galleryPreviews=" << previewed;

    auto out = structural_pipeline(probe, module_id, detail.str());
    out.modules.push_back({"native.texture-set", true});
    out.modules.push_back({"profiles.dmc3.texture-slot-framing", true});
    out.modules.push_back({"formats.dds.child-validation", true});
    if (set.ptx_community_descriptors) {
        out.modules.push_back({"native.ptx-community-descriptors", true});
        out.detail +=
            "\nPTX descriptors written by a community tool: read leniently (header, sector spans, DDS)";
    }
    if (set.ptx_aux_compat_used) {
        out.modules.push_back({"native.ptx-aux-compat", true});
        out.detail +=
            "\nPTX compatibility: retained corpus-confirmed DXT1 auxiliary mode without obsolete DXT5 coupling";
    }
    out.inspection.format = "PTX";
    out.inspection.root.id = "ptx";
    out.inspection.root.title = "PTX";
    out.inspection.root.kind = InspectionKind::Document;
    out.inspection.root.source_span = SourceSpan{0U, source.size()};
    out.inspection.root.properties.push_back({
        "TextureCount", std::to_string(set.slots.size()),
        EvidenceLevel::StructuralConfirmed});
    out.inspection.root.properties.push_back({
        "DDSBytes", std::to_string(total_dds_bytes),
        EvidenceLevel::StructuralConfirmed});
    out.inspection.root.children.push_back(std::move(texture_nodes));
    out.children = std::move(child_resources);
    return out;
}

struct TextureExecutionState final {
    const std::uint8_t* bytes{};
    std::size_t size{};
    const ProbeResult* probe{};
    const char* module_id{};
    textures::ParseResult set{};
    PipelineResult result{};
};

bool frame_ptx_operation(void* raw, std::uint32_t) noexcept {
    auto* state = static_cast<TextureExecutionState*>(raw);
    if (state == nullptr || state->probe == nullptr || state->module_id == nullptr) {
        return false;
    }
    try {
        if (state->bytes == nullptr) {
            state->result = module_support::reject(
                *state->probe, state->module_id, "PTX rejected: null input");
            return false;
        }

        state->set = textures::parse_ptx(as_bytes(state->bytes, state->size));
        if (!state->set.ok() || state->set.kind != textures::Kind::ptx_bundle) {
            state->result = module_support::reject(
                *state->probe, state->module_id,
                state->set.detail.empty()
                    ? "PTX rejected by TextureSet"
                    : state->set.detail);
            return false;
        }
        return true;
    } catch (...) {
        state->result = module_support::reject_minimal(*state->probe);
        return false;
    }
}

bool project_texture_operation(void* raw, std::uint32_t) noexcept {
    auto* state = static_cast<TextureExecutionState*>(raw);
    if (state == nullptr || state->probe == nullptr || state->module_id == nullptr) {
        return false;
    }

    try {
        if (state->probe->format == Format::Dds) {
            if (state->bytes == nullptr) {
                state->result = module_support::reject(
                    *state->probe, state->module_id, "DDS rejected: null input");
                return false;
            }
            state->set = textures::parse_dds(as_bytes(state->bytes, state->size));
            state->result = run_dds_set(
                as_bytes(state->bytes, state->size), state->set,
                *state->probe, state->module_id);
            return state->result.accepted;
        }

        if (state->probe->format == Format::Ptx) {
            if (state->bytes == nullptr || !state->set.ok() ||
                state->set.kind != textures::Kind::ptx_bundle) {
                state->result = module_support::reject(
                    *state->probe, state->module_id,
                    "PTX rejected: Crusader TextureSet dependency is unavailable");
                return false;
            }
            state->result = run_ptx_set(
                as_bytes(state->bytes, state->size), state->set,
                *state->probe, state->module_id);
            return state->result.accepted;
        }

        state->result = module_support::reject(
            *state->probe, state->module_id,
            "Texture pipeline rejected: unsupported route");
        return false;
    } catch (...) {
        state->result = module_support::reject_minimal(*state->probe);
        return false;
    }
}

const crusader::Plan& direct_dds_plan() {
    static const crusader::Plan plan = [] {
        crusader::Plan out;
        out.instructions.push_back(crusader::Instruction{
            .operation = kTextureProject,
            .operand = 0U,
            .dependency_begin = 0U,
            .dependency_count = 0U,
            .domain = crusader::Domain::cpu,
        });
        return out;
    }();
    return plan;
}

const crusader::Plan& ptx_plan() {
    static const crusader::Plan plan = [] {
        crusader::Plan out;
        out.dependencies.push_back(0U);
        out.instructions.push_back(crusader::Instruction{
            .operation = kTextureFramePtx,
            .operand = 0U,
            .dependency_begin = 0U,
            .dependency_count = 0U,
            .domain = crusader::Domain::cpu,
        });
        out.instructions.push_back(crusader::Instruction{
            .operation = kTextureProject,
            .operand = 0U,
            .dependency_begin = 0U,
            .dependency_count = 1U,
            .domain = crusader::Domain::cpu,
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
    try {
        TextureExecutionState state{
            .bytes = bytes,
            .size = size,
            .probe = &probe,
            .module_id = module.id,
        };

        static const std::array bindings{
            crusader::OperationBinding{
                .operation = kTextureFramePtx,
                .execute = &frame_ptx_operation,
            },
            crusader::OperationBinding{
                .operation = kTextureProject,
                .execute = &project_texture_operation,
            },
        };

        const crusader::Plan* plan = nullptr;
        if (module.format == Format::Dds) {
            plan = &direct_dds_plan();
        } else if (module.format == Format::Ptx) {
            plan = &ptx_plan();
        }

        if (plan == nullptr) {
            return module_support::reject(
                probe, module.id, "Texture pipeline rejected: invalid module route");
        }

        const auto report = crusader::execute(*plan, bindings, &state);
        if (!report.ok()) {
            if (!state.result.detail.empty()) return state.result;
            std::string detail = "Crusader texture execution failed: ";
            detail += crusader::to_string(report.status);
            return module_support::reject(probe, module.id, std::move(detail));
        }

        state.result.modules.push_back({"spider.crusader", true});
        return state.result;
    } catch (...) {
        return module_support::reject_minimal(probe);
    }
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
