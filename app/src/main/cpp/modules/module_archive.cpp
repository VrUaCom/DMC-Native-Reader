#include "dmcresource/native_module.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <span>
#include <sstream>
#include <string>
#include <utility>

#include "dmc_rengine/formats/mot/parser.hpp"
#include "dmc_rengine/formats/pac.hpp"
#include "dmc_rengine/formats/pnst.hpp"
#include "dmcresource/archive_entry.h"
#include "dmcresource/module_support.h"
#include "dmcresource/motion/cloth_chain.h"
#include "dmcresource/motion/uv_scroll.h"
#include "dmcresource/ptx_framing_compat.h"
#include "dmcresource/resource_limits.h"

namespace dmcresource {
namespace archive {

namespace {

[[nodiscard]] bool magic_at(const std::uint8_t* bytes, std::size_t size, std::size_t offset,
                            const char (&magic)[5]) noexcept {
    if (bytes == nullptr || size < offset + 4U) return false;
    for (std::size_t i = 0U; i < 4U; ++i) {
        if (bytes[offset + i] != static_cast<std::uint8_t>(magic[i])) return false;
    }
    return true;
}

}  // namespace

EntryKind classify_payload(const std::uint8_t* bytes, std::size_t size) noexcept {
    if (magic_at(bytes, size, 0U, "MOD ")) return {Format::Mod, "MOD", "mod"};
    if (magic_at(bytes, size, 0U, "SCM ")) return {Format::Scm, "SCM", "scm"};
    if (magic_at(bytes, size, 0U, "DDS ")) return {Format::Dds, "DDS", "dds"};
    if (magic_at(bytes, size, 0U, "PAC\0")) return {Format::Pac, "PAC", "pac"};
    if (magic_at(bytes, size, 0U, "PNST")) return {Format::Pnst, "PNST", "pnst"};
    if (magic_at(bytes, size, 0U, "EVT\0")) return {Format::Evt, "EventTbl", "bin"};
    if (magic_at(bytes, size, 4U, "MOT\0")) return {Format::Mot, "MOT", "mot"};
    if (magic_at(bytes, size, 0U, "SHW ")) {
        return {Format::Shw, "SHW", "shw", true};
    }
    if (bytes != nullptr && size > 0U) {
        const std::string_view text{reinterpret_cast<const char*>(bytes), size};
        if (motion::looks_like_tsc(text)) return {Format::Tsc, "TSC", "tsc"};
        if (motion::looks_like_clt(text)) return {Format::Clt, "CLT", "clt"};
    }
    if (bytes != nullptr && size > 0U) {
        try {
            const auto parsed = ptx_compat::parse_texture_bundle(
                std::span<const std::byte>{reinterpret_cast<const std::byte*>(bytes), size});
            if (parsed.ok()) return {Format::Ptx, "PTX", "ptx"};
        } catch (...) {
        }
    }
    return {};
}

std::string slot_filename(std::uint32_t slot, const EntryKind& kind) {
    std::ostringstream out;
    out << "slot_" << std::setfill('0') << std::setw(4) << slot << '.' << kind.extension;
    return out.str();
}

}  // namespace archive

namespace {

[[nodiscard]] std::string size_text(std::uint64_t bytes) {
    std::ostringstream out;
    if (bytes >= 1024U * 1024U) {
        out << std::fixed << std::setprecision(1)
            << static_cast<double>(bytes) / (1024.0 * 1024.0) << " MiB";
    } else if (bytes >= 1024U) {
        out << std::fixed << std::setprecision(1) << static_cast<double>(bytes) / 1024.0 << " KiB";
    } else {
        out << bytes << " B";
    }
    return out.str();
}

PipelineResult run_pac_module(const NativeModule& module,
                              std::string_view,
                              const std::uint8_t* bytes,
                              std::size_t size,
                              const ProbeResult& probe) noexcept {
    try {
        const auto span = std::span<const std::byte>{
            reinterpret_cast<const std::byte*>(bytes), size};
        const bool pnst = module.format == Format::Pnst;
        const auto parsed = pnst ? dmc::rengine::formats::PnstParser::parse(span)
                                 : dmc::rengine::formats::PacParser::parse(span);
        const char* family = pnst ? "PNST" : "PAC";
        if (!parsed.ok()) {
            return module_support::reject(
                probe, module.id,
                std::string{family} + " rejected by canonical parser: " + parsed.message);
        }
        const auto& document = *parsed.document;

        PipelineResult out;
        out.accepted = true;
        out.renderable = false;
        out.probe = probe;
        out.modules.push_back({"identity-probe", true});
        out.modules.push_back({"bounded-read-guard", true});
        out.modules.push_back({pnst ? "canonical.pnst.relative-slot-container"
                                    : "canonical.pac.relative-slot-container", true});
        out.modules.push_back({module.id, true});

        out.inspection.format = family;
        out.inspection.root.id = pnst ? "pnst" : "pac";
        out.inspection.root.title = std::string{family} + " archive";
        out.inspection.root.kind = InspectionKind::Document;
        out.inspection.root.source_span = SourceSpan{0U, size};
        out.inspection.root.properties.push_back({
            "DeclaredSlots", std::to_string(document.declared_slot_count),
            EvidenceLevel::StructuralConfirmed});

        InspectionNode entries;
        entries.id = "entries";
        entries.title = "Entries";
        entries.kind = InspectionKind::Collection;

        std::size_t populated = 0U;
        std::size_t by_format[16]{};
        std::size_t shadows = 0U;
        for (const auto& entry : document.entries) {
            if (!entry.populated || entry.size == 0U || !entry.valid(document.container_size)) {
                continue;
            }
            ++populated;
            const auto* payload = bytes + static_cast<std::size_t>(entry.offset);
            const auto payload_size = static_cast<std::size_t>(entry.size);
            const auto kind = archive::classify_payload(payload, payload_size);
            ++by_format[static_cast<std::size_t>(kind.format) & 15U];
            if (kind.shadow) ++shadows;

            ChildResource child;
            child.id = "slot-" + std::to_string(entry.slot_index);
            child.suggested_filename = archive::slot_filename(entry.slot_index, kind);
            child.title = "#" + std::to_string(entry.slot_index) + " · " + kind.family +
                          " · " + size_text(entry.size);
            child.source_span = SourceSpan{entry.offset, entry.size};
            child.probe = dmcresource::probe(child.suggested_filename, payload, payload_size);
            child.capabilities = capability(ResourceCapability::Inspection);
            child.source_bytes.assign(payload, payload + payload_size);
            child.detail = std::string{kind.family} + " payload in " + family + " slot " +
                           std::to_string(entry.slot_index) + " (" + size_text(entry.size) + ")";
            child.trace = std::string{"[OK] canonical."} + (pnst ? "pnst" : "pac") +
                          ".relative-slot-container\n[OK] native.archive.classify";
            child.inspection.format = kind.family;
            child.inspection.root.id = child.id;
            child.inspection.root.title = child.title;
            child.inspection.root.kind = InspectionKind::Document;
            child.inspection.root.source_span = child.source_span;

            InspectionNode node;
            node.id = child.id;
            node.title = child.title;
            node.kind = InspectionKind::Object;
            node.source_span = child.source_span;
            node.properties.push_back({"Family", kind.family, EvidenceLevel::DataConfirmed});
            entries.children.push_back(std::move(node));
            out.children.push_back(std::move(child));
        }
        out.inspection.root.properties.push_back({
            "PopulatedSlots", std::to_string(populated), EvidenceLevel::StructuralConfirmed});
        out.inspection.root.children.push_back(std::move(entries));

        std::ostringstream detail;
        detail << family << " read-only archive | slots=" << document.declared_slot_count
               << " populated=" << populated
               << " MOD=" << by_format[static_cast<std::size_t>(Format::Mod)]
               << " PTX=" << by_format[static_cast<std::size_t>(Format::Ptx)]
               << " MOT=" << by_format[static_cast<std::size_t>(Format::Mot)]
               << " PAC=" << (by_format[static_cast<std::size_t>(Format::Pac)] +
                              by_format[static_cast<std::size_t>(Format::Pnst)])
               << " SHW=" << shadows;
        out.detail = detail.str();
        return out;
    } catch (...) {
        return module_support::reject_minimal(probe);
    }
}

PipelineResult run_mot_module(const NativeModule& module,
                              std::string_view,
                              const std::uint8_t* bytes,
                              std::size_t size,
                              const ProbeResult& probe) noexcept {
    try {
        const auto span = std::span<const std::byte>{
            reinterpret_cast<const std::byte*>(bytes), size};
        const auto parsed = dmc::rengine::formats::mot::Parser::parse(span);
        if (!parsed.ok()) {
            return module_support::reject(probe, module.id,
                                          "MOT rejected by canonical parser: " + parsed.message);
        }
        const auto& document = *parsed.document;
        std::size_t comp2 = 0U;
        std::size_t comp3 = 0U;
        std::size_t other = 0U;
        std::size_t keys = 0U;
        for (const auto& track : document.tracks) {
            keys += track.key_count;
            if (track.compression == 2U) ++comp2;
            else if (track.compression == 3U) ++comp3;
            else ++other;
        }

        PipelineResult out;
        out.accepted = true;
        out.renderable = false;
        out.probe = probe;
        out.modules.push_back({"identity-probe", true});
        out.modules.push_back({"bounded-read-guard", true});
        out.modules.push_back({"canonical.mot.parser", true});
        out.modules.push_back({module.id, true});
        out.inspection.format = "MOT";
        out.inspection.root.id = "mot";
        out.inspection.root.title = "MOT motion";
        out.inspection.root.kind = InspectionKind::Document;
        out.inspection.root.source_span = SourceSpan{0U, size};
        const auto add = [&out](const char* name, std::string value, EvidenceLevel level) {
            out.inspection.root.properties.push_back({name, std::move(value), level});
        };
        add("SkeletonNodes", std::to_string(document.channel_domain_count),
            EvidenceLevel::ExeConfirmed);
        add("Tracks", std::to_string(document.tracks.size()), EvidenceLevel::ExeConfirmed);
        add("Keys", std::to_string(keys), EvidenceLevel::StructuralConfirmed);
        add("Compression3Tracks", std::to_string(comp3), EvidenceLevel::ExeConfirmed);
        add("Compression2Tracks", std::to_string(comp2), EvidenceLevel::ExeConfirmed);
        add("OtherCompressionTracks", std::to_string(other), EvidenceLevel::Recognized);
        add("EndFrame(+0x0C)", std::to_string(document.raw_f32_0c), EvidenceLevel::DataConfirmed);
        add("Header+0x10", std::to_string(document.raw_f32_10), EvidenceLevel::DataConfirmed);
        add("Playback", "stage this MOT on a MOD with the same node count",
            EvidenceLevel::Recognized);

        std::ostringstream detail;
        detail << "MOT canonical reader | nodes=" << document.channel_domain_count
               << " tracks=" << document.tracks.size() << " comp3=" << comp3
               << " comp2=" << comp2 << " other=" << other
               << " endFrame=" << document.raw_f32_0c;
        out.detail = detail.str();
        return out;
    } catch (...) {
        return module_support::reject_minimal(probe);
    }
}

}  // namespace

NativeModule pac_module() noexcept {
    return {
        "formats.pac.archive-reader",
        "PAC",
        Format::Pac,
        ModuleKind::Structural,
        false,
        run_pac_module,
        capability(ResourceCapability::Inspection) | ResourceCapability::ChildResources |
            ResourceCapability::Container,
    };
}

NativeModule pnst_module() noexcept {
    return {
        "formats.pnst.archive-reader",
        "PNST",
        Format::Pnst,
        ModuleKind::Structural,
        false,
        run_pac_module,
        capability(ResourceCapability::Inspection) | ResourceCapability::ChildResources |
            ResourceCapability::Container,
    };
}

NativeModule mot_module() noexcept {
    return {
        "formats.mot.motion-reader",
        "MOT",
        Format::Mot,
        ModuleKind::Structural,
        false,
        run_mot_module,
        capability(ResourceCapability::Inspection),
    };
}

}  // namespace dmcresource
