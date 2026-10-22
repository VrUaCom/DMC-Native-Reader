#include "dmcresource/native_module.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "dmc_rengine/formats/dca.hpp"
#include "dmc_rengine/formats/lig2.hpp"
#include "dmc_rengine/formats/so/graph.hpp"
#include "dmc_rengine/formats/stage_txt.hpp"
#include "dmcresource/decode_pipeline.h"
#include "dmcresource/module_support.h"
#include "dmcresource/raster_card.h"
#include "dmcresource/sound.h"

// The remaining stage and enemy families, each opening with its Info:
// LIG2 lighting tables, DCA records, stage text (#SET, DOOR, BOXIN,
// NEXTROOM), .index name lists, SO graphs, dbsT
// sound tables, ff&A float tables, HID dispatch payloads, the SPUMAPDT map
// and VAGp sound. Readers from dmc-rengine-cpp where it has them (LIG2,
// DCA, stage text, SO); the rest from the stage corpus reverse notes. Field
// names are given only where they are settled; everything else is shown as
// the numbers it holds. The SO volume and link tables of dmc-rengine-cpp are
// not here: they are the collision shapes and attack indices (COLSHAPE,
// COLINDEX) the Reader already reads from the EXE.
namespace dmcresource {
namespace {

namespace rf = dmc::rengine::formats;

[[nodiscard]] std::string hex(std::uint64_t value) {
    char text[24];
    std::snprintf(text, sizeof text, "0x%llX", static_cast<unsigned long long>(value));
    return text;
}

[[nodiscard]] std::string number(float value) {
    char text[32];
    std::snprintf(text, sizeof text, "%.4g", static_cast<double>(value));
    return text;
}

[[nodiscard]] std::uint32_t u32(std::span<const std::uint8_t> b, std::size_t at) noexcept {
    std::uint32_t v = 0U;
    std::memcpy(&v, b.data() + at, 4U);
    return v;
}

[[nodiscard]] float f32(std::span<const std::uint8_t> b, std::size_t at) noexcept {
    float v = 0.0F;
    std::memcpy(&v, b.data() + at, 4U);
    return v;
}

void property(InspectionNode& node, std::string key, std::string value,
              EvidenceLevel evidence = EvidenceLevel::DataConfirmed) {
    node.properties.push_back({std::move(key), std::move(value), evidence});
}

void add_diagnostics(InspectionNode& root, const std::vector<rf::ParseDiagnostic>& diagnostics) {
    if (diagnostics.empty()) return;
    InspectionNode notes;
    notes.id = "notes";
    notes.title = "Reader notes";
    notes.kind = InspectionKind::Diagnostic;
    for (const auto& d : diagnostics) {
        property(notes, std::string{rf::to_string(d.severity)} + " at " + hex(d.offset), d.message,
                 EvidenceLevel::StructuralConfirmed);
    }
    root.children.push_back(std::move(notes));
}

// One record as a row of floats (or u32 where a float would not be a number).
[[nodiscard]] std::string words_text(std::span<const std::uint8_t> b, std::size_t at, std::size_t words) {
    std::string text;
    for (std::size_t w = 0; w < words && at + w * 4U + 4U <= b.size(); ++w) {
        const float f = f32(b, at + w * 4U);
        const auto raw = u32(b, at + w * 4U);
        if (!text.empty()) text += "  ";
        const bool readable = std::isfinite(f) && (raw == 0U || (std::fabs(f) > 1.0e-6F && std::fabs(f) < 1.0e7F));
        text += readable ? number(f) : hex(raw);
    }
    return text;
}

void add_rows(InspectionNode& root, std::string_view title, std::span<const std::uint8_t> b, std::size_t first,
              std::size_t stride, std::size_t count, std::size_t max_rows = 256U) {
    InspectionNode rows;
    rows.id = "records";
    rows.title = std::string{title};
    rows.kind = InspectionKind::Collection;
    for (std::size_t r = 0; r < count && r < max_rows; ++r) {
        property(rows, std::to_string(r) + " @" + hex(first + r * stride),
                 words_text(b, first + r * stride, stride / 4U), EvidenceLevel::PreservedUndecoded);
    }
    if (count > max_rows) property(rows, "...", std::to_string(count - max_rows) + " more");
    root.children.push_back(std::move(rows));
}

PipelineResult start(const NativeModule& module, const ProbeResult& probe, std::size_t size, std::string title) {
    PipelineResult out;
    out.accepted = true;
    out.renderable = false;
    out.probe = probe;
    out.modules.push_back({"identity-probe", true});
    out.modules.push_back({"bounded-read-guard", true});
    out.modules.push_back({module.id, true});
    out.inspection.format = module.family;
    out.inspection.root.id = module.family;
    out.inspection.root.title = std::move(title);
    out.inspection.root.kind = InspectionKind::Document;
    out.inspection.root.source_span = SourceSpan{0U, size};
    return out;
}

PipelineResult run_family(const NativeModule& module, std::string_view filename, const std::uint8_t* bytes,
                          std::size_t size, const ProbeResult& probe) noexcept {
    try {
        const std::span<const std::uint8_t> data{bytes, size};
        const auto raw = std::as_bytes(data);
        switch (module.format) {
        case Format::Lig2: {
            const auto scan = rf::lig2::RecordScanner::scan(raw);
            if (!scan.ok() || size < 4U || std::memcmp(bytes, "LIG2", 4U) != 0) break;
            auto out = start(module, probe, size, "LIG2 - stage lighting table");
            auto& root = out.inspection.root;
            property(root, "Records", std::to_string(scan.records.size()) + " of 0x30 bytes after a 0x20-byte header",
                     EvidenceLevel::StructuralConfirmed);
            property(root, "Fields", "names open; each record shown as its twelve words",
                     EvidenceLevel::PreservedUndecoded);
            add_rows(root, "Records", data, rf::lig2::header_size, rf::lig2::record_size, scan.records.size());
            add_diagnostics(root, scan.diagnostics);
            out.detail = "LIG2 | " + std::to_string(scan.records.size()) + " records";
            return out;
        }
        case Format::Dca: {
            const auto scan = rf::dca::RecordScanner::scan(raw);
            if (!scan.ok()) break;
            auto out = start(module, probe, size, "DCA - stage records");
            auto& root = out.inspection.root;
            property(root, "Records", std::to_string(scan.records.size()) + " of 0x410 bytes after a 0x10-byte header",
                     EvidenceLevel::StructuralConfirmed);
            property(root, "Fields", "names open", EvidenceLevel::PreservedUndecoded);
            add_rows(root, "First words of each record", data, rf::dca::header_size, rf::dca::record_size,
                     scan.records.size());
            add_diagnostics(root, scan.diagnostics);
            out.detail = "DCA | " + std::to_string(scan.records.size()) + " records";
            return out;
        }
        case Format::StageText: {
            const auto lex = rf::stage_txt::Lexer::scan(raw);
            if (!lex.ok()) break;
            auto out = start(module, probe, size, "Stage text - room setup");
            auto& root = out.inspection.root;
            using rf::stage_txt::TokenKind;
            const auto count = [&](TokenKind kind) { return std::to_string(lex.by_kind(kind).size()); };
            property(root, "#SET", count(TokenKind::directive_set));
            property(root, "DOOR", count(TokenKind::door));
            property(root, "BOXIN", count(TokenKind::box_in));
            property(root, "NEXTROOM", count(TokenKind::next_room));
            property(root, "Stage-set values", count(TokenKind::stage_set_value));
            property(root, "Tokens", std::to_string(lex.tokens.size()));
            // Each directive line: the directive and what follows it on that line.
            InspectionNode lines;
            lines.id = "directives";
            lines.title = "Directives";
            lines.kind = InspectionKind::Collection;
            for (std::size_t i = 0; i < lex.tokens.size() && lines.properties.size() < 400U; ++i) {
                const auto& t = lex.tokens[i];
                if (t.kind != TokenKind::directive_set && t.kind != TokenKind::door && t.kind != TokenKind::box_in &&
                    t.kind != TokenKind::next_room) {
                    continue;
                }
                std::string rest;
                for (std::size_t j = i + 1U; j < lex.tokens.size() && lex.tokens[j].line == t.line; ++j) {
                    rest += (rest.empty() ? "" : " ") + lex.tokens[j].value;
                }
                property(lines, "line " + std::to_string(t.line) + " " + t.value, rest);
            }
            root.children.push_back(std::move(lines));
            add_diagnostics(root, lex.diagnostics);
            out.detail = "Stage text | " + count(TokenKind::door) + " doors";
            return out;
        }
        case Format::NameIndex: {
            auto out = start(module, probe, size, "Name index - container slot names");
            auto& root = out.inspection.root;
            InspectionNode names;
            names.id = "names";
            names.title = "Slots";
            names.kind = InspectionKind::Collection;
            std::size_t slot = 0U, at = 0U;
            bool marker = false;
            while (at < size) {
                auto end = std::find(data.begin() + static_cast<std::ptrdiff_t>(at), data.end(), '\n') - data.begin();
                std::string line(reinterpret_cast<const char*>(bytes + at), static_cast<std::size_t>(end) - at);
                while (!line.empty() && (line.back() == '\r' || line.back() == '\0')) line.pop_back();
                at = static_cast<std::size_t>(end) + 1U;
                if (line.empty()) continue;
                if (slot == 0U && !marker && line == "PNST") {
                    marker = true;
                    continue;
                }
                if (names.properties.size() < 1024U) property(names, "slot " + std::to_string(slot), line);
                ++slot;
            }
            property(root, "Names", std::to_string(slot));
            if (marker) property(root, "Marker line", "PNST");
            root.children.push_back(std::move(names));
            out.detail = "Name index | " + std::to_string(slot) + " names";
            return out;
        }
        case Format::SoGraph: {
            const auto parsed = rf::so::graph::parse(raw);
            if (!parsed.ok()) break;
            auto out = start(module, probe, size, "SO graph - indexed blocks");
            auto& root = out.inspection.root;
            property(root, "Blocks", std::to_string(parsed.blocks.size()), EvidenceLevel::StructuralConfirmed);
            for (std::size_t i = 0; i < parsed.blocks.size(); ++i) {
                const auto& block = parsed.blocks[i];
                InspectionNode node;
                node.id = "block-" + std::to_string(i);
                node.title = "Block " + std::to_string(i) + " (type " + std::to_string(block.type) + ")";
                node.kind = InspectionKind::Object;
                node.source_span = SourceSpan{block.base_offset, block.extent_size};
                property(node, "At", hex(block.base_offset));
                property(node, "Size", std::to_string(block.extent_size));
                property(node, "Entries", std::to_string(block.entry_offsets.size()));
                root.children.push_back(std::move(node));
            }
            add_diagnostics(root, parsed.diagnostics);
            out.detail = "SO graph | " + std::to_string(parsed.blocks.size()) + " blocks";
            return out;
        }
        case Format::SoundTable: {
            sound::SoundTable table;
            if (!sound::read_sound_table(data, &table)) break;
            auto out = start(module, probe, size, "dbsT - sound table");
            auto& root = out.inspection.root;
            property(root, "Records", std::to_string(table.records) + " of 32 bytes after a 16-byte header",
                     EvidenceLevel::StructuralConfirmed);
            property(root, "Header words", hex(table.header[0]) + "  " + hex(table.header[1]) + "  " + hex(table.header[2]));
            property(root, "Count field",
                     table.count_field < 0 ? "no header word equals the record count"
                                           : "the word at +" + std::to_string(4 + table.count_field * 4) +
                                                 " equals the record count");
            if (table.trailing != 0U) property(root, "Trailing bytes", std::to_string(table.trailing));
            add_rows(root, "Records", data, 16U, 32U, table.records);
            out.detail = "dbsT | " + std::to_string(table.records) + " sounds";
            return out;
        }
        case Format::FloatTable: {
            if (size < 16U) break;
            auto out = start(module, probe, size, "ff&A - float table");
            auto& root = out.inspection.root;
            const std::size_t rows = (size - 16U) / 16U;
            property(root, "Rows", std::to_string(rows) + " of 16 bytes after a 16-byte header",
                     EvidenceLevel::StructuralConfirmed);
            property(root, "Header", words_text(data, 4U, 3U), EvidenceLevel::PreservedUndecoded);
            add_rows(root, "Rows (four floats)", data, 16U, 16U, rows);
            out.detail = "ff&A | " + std::to_string(rows) + " rows";
            return out;
        }
        case Format::Hid: {
            if (size < 8U) break;
            auto out = start(module, probe, size, "HID - dispatch payload");
            auto& root = out.inspection.root;
            property(root, "Word at +0", hex(u32(data, 0U)), EvidenceLevel::PreservedUndecoded);
            property(root, "Tag", "HID at +4 (the EXE dispatches .hid by extension)", EvidenceLevel::ExeConfirmed);
            property(root, "Size", std::to_string(size) + " bytes; fields open", EvidenceLevel::PreservedUndecoded);
            out.detail = "HID | " + std::to_string(size) + " bytes";
            return out;
        }
        case Format::SpuMap: {
            auto out = start(module, probe, size, "SPUMAPDT - sound memory map");
            property(out.inspection.root, "Size", std::to_string(size) + " bytes; fields open",
                     EvidenceLevel::PreservedUndecoded);
            out.detail = "SPUMAPDT | " + std::to_string(size) + " bytes";
            return out;
        }
        case Format::Ogg: {
            const auto pcm = sound::decode_ogg(data);
            if (!pcm) break;
            auto out = start(module, probe, size, "OGG - Ogg Vorbis sound");
            auto& root = out.inspection.root;
            property(root, "Sample rate", std::to_string(pcm->sample_rate) + " Hz");
            property(root, "Channels", std::to_string(pcm->channels));
            char seconds[32];
            std::snprintf(seconds, sizeof seconds, "%.2f s",
                          static_cast<double>(pcm->frames()) / static_cast<double>(pcm->sample_rate));
            property(root, "Length", std::string{seconds} + ", " + std::to_string(pcm->frames()) + " frames");
            property(root, "Source", "HD release: the game's ADX streams renamed to .ogg (0x140031D80)",
                     EvidenceLevel::ExeConfirmed);
            out.image_preview = sound::render_waveform(sound::mono(*pcm), pcm->sample_rate,
                                                       "OGG " + std::string{filename});
            out.detail = "OGG | " + std::to_string(pcm->sample_rate) + " Hz, " + std::to_string(pcm->channels) +
                         " channel(s), " + seconds;
            return out;
        }
        case Format::Vag: {
            const auto chunks = sound::read_vag(data);
            if (chunks.empty()) break;
            auto out = start(module, probe, size, "VAG - PS-ADPCM sound");
            auto& root = out.inspection.root;
            property(root, "Sounds", std::to_string(chunks.size()), EvidenceLevel::StructuralConfirmed);
            std::vector<std::int16_t> first;
            for (std::size_t i = 0; i < chunks.size() && i < 512U; ++i) {
                const auto& c = chunks[i];
                const auto samples = sound::decode_vag(data, c);
                if (i == 0U) first = samples;
                InspectionNode node;
                node.id = "vag-" + std::to_string(i);
                node.title = "Sound " + std::to_string(i) + (c.name.empty() ? "" : " - " + c.name);
                node.kind = InspectionKind::Object;
                node.source_span = SourceSpan{c.offset, 0x30U + c.data_size};
                property(node, "At", hex(c.offset));
                property(node, "Sample rate", std::to_string(c.sample_rate) + " Hz");
                property(node, "Data", std::to_string(c.data_size) + " bytes, " + std::to_string(c.frames) + " frames");
                char seconds[32];
                std::snprintf(seconds, sizeof seconds, "%.2f s",
                              c.sample_rate == 0U ? 0.0 : static_cast<double>(samples.size()) / c.sample_rate);
                property(node, "Length", seconds);
                property(node, "Version", hex(c.version), EvidenceLevel::StructuralConfirmed);
                root.children.push_back(std::move(node));
            }
            const auto& c0 = chunks.front();
            out.image_preview = sound::render_waveform(
                first, c0.sample_rate,
                std::string{"VAG "} + (c0.name.empty() ? std::string{filename} : c0.name) +
                    (chunks.size() > 1U ? "  (FIRST OF " + std::to_string(chunks.size()) + ")" : ""));
            out.detail = "VAG | " + std::to_string(chunks.size()) + " sound(s), " + std::to_string(c0.sample_rate) + " Hz";
            return out;
        }
        default: break;
        }
        return module_support::reject(probe, module.id, std::string{"not a "} + module.family + " resource");
    } catch (...) {
        return module_support::reject(probe, module.id, "family module failed");
    }
}

[[nodiscard]] NativeModule make(const char* id, const char* family, Format format, bool image = false) noexcept {
    auto capabilities = capability(ResourceCapability::Inspection);
    if (image) capabilities = capabilities | ResourceCapability::ImagePreview;
    return {id, family, format, ModuleKind::Structural, false, run_family, capabilities};
}

}  // namespace

std::vector<NativeModule> family_modules() noexcept {
    return {
        make("formats.lig2.records", "LIG2", Format::Lig2),
        make("formats.dca.records", "DCA", Format::Dca),
        make("formats.stage-txt.lexer", "TXT", Format::StageText),
        make("formats.name-index", ".index", Format::NameIndex),
        make("formats.so.graph", "SO-GRAPH", Format::SoGraph),
        make("formats.sound.dbst", "dbsT", Format::SoundTable),
        make("formats.ff-table", "ff&A", Format::FloatTable),
        make("formats.hid", "HID", Format::Hid),
        make("formats.spumap", "SPUMAPDT", Format::SpuMap),
        make("formats.sound.vag", "VAG", Format::Vag, true),
        make("formats.sound.ogg", "OGG", Format::Ogg, true),
    };
}

}  // namespace dmcresource
