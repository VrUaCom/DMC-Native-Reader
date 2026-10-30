#include "dmcresource/native_module.h"

#include <algorithm>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

#include "dmcresource/decode_pipeline.h"
#include "dmcresource/module_support.h"
#include "dmcresource/raster_card.h"
#include "dmcresource/stage_config.h"
#include "dmcresource/stage_markers.h"

// The stage configuration members (stNNNcfg.pac): POS, EVE, CAM, ITM, STE,
// EST and SEF. Each opens with its Info (every record, read from the bytes)
// and its own picture: a top-view map of what it places, the difficulty
// table of EST programs, or the section layout of SEF.
namespace dmcresource {
namespace {

using namespace stage_config;

[[nodiscard]] std::string hex(std::uint64_t value) {
    char text[24];
    std::snprintf(text, sizeof text, "0x%llX", static_cast<unsigned long long>(value));
    return text;
}

[[nodiscard]] Kind kind_of(Format format) noexcept {
    switch (format) {
    case Format::StagePos: return Kind::Pos;
    case Format::StageEve: return Kind::Eve;
    case Format::StageCam: return Kind::Cam;
    case Format::StageItm: return Kind::Itm;
    case Format::StageSte: return Kind::Ste;
    case Format::StageEst: return Kind::Est;
    default: return Kind::Sef;
    }
}

void property(InspectionNode& node, std::string key, std::string value,
              EvidenceLevel evidence = EvidenceLevel::DataConfirmed) {
    node.properties.push_back({std::move(key), std::move(value), evidence});
}

template <typename Document>
void add_notes(InspectionNode& root, const Result<Document>& read) {
    if (read.notes.empty()) return;
    InspectionNode notes;
    notes.id = "notes";
    notes.title = "Reader notes";
    notes.kind = InspectionKind::Diagnostic;
    for (const auto& note : read.notes) {
        const char* level = note.severity == Severity::Error ? "error"
                            : note.severity == Severity::Warning ? "warning" : "note";
        property(notes, std::string{level} + " at " + hex(note.offset), note.message,
                 EvidenceLevel::StructuralConfirmed);
    }
    root.children.push_back(std::move(notes));
}

// Every marker as an Info child: its title, then its description lines.
void add_markers(InspectionNode& root, const std::vector<stage_markers::Marker>& markers) {
    for (const auto& marker : markers) {
        InspectionNode node;
        node.id = marker.label;
        node.title = marker.title;
        node.kind = InspectionKind::Object;
        node.source_span = SourceSpan{marker.offset, 0U};
        for (const auto& [name, value] : marker.fields) property(node, name, value);
        root.children.push_back(std::move(node));
    }
}

// EST: rows of programs by difficulty, each cell the program it runs.
ImagePreview render_est(const EstDocument& doc) {
    raster::Canvas canvas(1080, 1080);
    canvas.fill(0, 0, 1080, 1080, raster::kBackground);
    const int scale = 2;
    canvas.text(16, 12, "EST  enemy set programs by difficulty", raster::kLabel, 3);
    const int top = 60, row_h = 26, col0 = 16, col_w = 200;
    canvas.text(col0, top, "ROW", raster::kDim, scale);
    for (std::size_t mode = 0; mode < kEstModes; ++mode) {
        canvas.text(col0 + 80 + static_cast<int>(mode) * col_w * 9 / 10, top, kEstModeNames[mode], raster::kDim,
                    scale);
    }
    int y = top + row_h;
    for (std::size_t row = 0; row < doc.rows.size() && y + row_h < 1080; ++row, y += row_h) {
        canvas.text(col0, y, std::to_string(row), raster::kLabel, scale);
        for (std::size_t mode = 0; mode < kEstModes; ++mode) {
            const auto index = doc.rows[row][mode];
            std::string cell = "-";
            if (index >= 0 && static_cast<std::size_t>(index) < doc.programs.size()) {
                cell = "P" + std::to_string(index) + " " +
                       std::to_string(doc.programs[static_cast<std::size_t>(index)].commands.size()) + " cmd";
            }
            canvas.text(col0 + 80 + static_cast<int>(mode) * col_w * 9 / 10, y, cell,
                        index >= 0 ? raster::kAccent : raster::kDim, scale);
        }
    }
    return canvas.take();
}

// SEF: the sections laid out as bars over the file.
ImagePreview render_sef(const SefDocument& doc, std::size_t size) {
    raster::Canvas canvas(1080, 720);
    canvas.fill(0, 0, 1080, 720, raster::kBackground);
    canvas.text(16, 12, "SEF  stage effect sections", raster::kLabel, 3);
    const int left = 16, right = 1064, top = 70;
    const auto to_x = [&](std::uint64_t at) {
        return left + static_cast<int>(static_cast<double>(at) / static_cast<double>(std::max<std::size_t>(size, 1U)) *
                                       (right - left));
    };
    canvas.fill(left, top, right, top + 30, raster::kPanel);
    for (std::size_t i = 0; i < doc.sections.size(); ++i) {
        const auto& s = doc.sections[i];
        const raster::Rgb color = i % 2U == 0U ? raster::Rgb{80, 200, 240} : raster::Rgb{250, 160, 60};
        canvas.fill(to_x(s.offset), top, std::max(to_x(s.offset + s.size), to_x(s.offset) + 1), top + 30, color);
        const int y = top + 50 + static_cast<int>(i) * 24;
        if (y > 700) break;
        canvas.text(left, y,
                    "SECTION " + std::to_string(i) + "  AT " + hex(s.offset) + "  " + std::to_string(s.size) +
                        " BYTES  VALUE " + std::to_string(s.value),
                    color, 2);
    }
    return canvas.take();
}

PipelineResult run_stage_config(const NativeModule& module, std::string_view, const std::uint8_t* bytes,
                                std::size_t size, const ProbeResult& probe) noexcept {
    try {
        const Bytes data{bytes, size};
        const auto kind = kind_of(module.format);
        PipelineResult out;
        out.accepted = true;
        out.renderable = false;
        out.probe = probe;
        out.modules.push_back({"identity-probe", true});
        out.modules.push_back({"bounded-read-guard", true});
        out.modules.push_back({module.id, true});
        auto& root = out.inspection.root;
        out.inspection.format = kind_name(kind);
        root.id = kind_name(kind);
        root.kind = InspectionKind::Document;
        root.source_span = SourceSpan{0U, size};
        root.title = std::string{kind_name(kind)} + " - " + stage_markers::kind_meaning(kind);

        const auto header_props = [&](const Header& header, std::size_t records) {
            property(root, "Version", std::to_string(header.version), EvidenceLevel::StructuralConfirmed);
            property(root, "Records declared", std::to_string(header.count), EvidenceLevel::StructuralConfirmed);
            property(root, "Records read", std::to_string(records));
        };
        const auto plan = [&](const std::vector<stage_markers::Marker>& markers) {
            add_markers(root, markers);
            out.image_preview = stage_markers::render_plan(
                markers, std::string{kind_name(kind)} + "  " + stage_markers::kind_meaning(kind));
        };
        std::string reject;
        std::size_t count = 0U;

        switch (kind) {
        case Kind::Pos: {
            const auto read = read_pos(data);
            if (!read.ok()) { reject = read.notes.empty() ? "" : read.notes.back().message; break; }
            header_props(read.document.header, read.document.records.size());
            property(root, "Room in the file", std::to_string(read.document.capacity) + " records");
            plan(stage_markers::read_markers(kind, data));
            add_notes(root, read);
            count = read.document.records.size();
            break;
        }
        case Kind::Eve: {
            const auto read = read_eve(data);
            if (!read.ok()) { reject = read.notes.empty() ? "" : read.notes.back().message; break; }
            header_props(read.document.header, read.document.records.size());
            plan(stage_markers::read_markers(kind, data));
            add_notes(root, read);
            count = read.document.records.size();
            break;
        }
        case Kind::Cam: {
            const auto read = read_cam(data);
            if (!read.ok()) { reject = read.notes.empty() ? "" : read.notes.back().message; break; }
            const auto& doc = read.document;
            header_props(doc.header, doc.records.size());
            property(root, "Zone records (0x400)", std::to_string(doc.records.size()));
            property(root, "Zones", std::to_string(doc.areas.size()));
            property(root, "Paths", std::to_string(doc.paths.size()));
            property(root, "Coefficient runs", std::to_string(doc.coefficient_runs.size()));
            property(root, "Bytes no shape explains", std::to_string(doc.unexplained_bytes),
                     EvidenceLevel::PreservedUndecoded);
            plan(stage_markers::read_markers(kind, data));
            add_notes(root, read);
            count = doc.areas.size() + doc.paths.size();
            break;
        }
        case Kind::Itm: {
            const auto read = read_itm(data);
            if (!read.ok()) { reject = read.notes.empty() ? "" : read.notes.back().message; break; }
            header_props(read.document.header, read.document.records.size());
            plan(stage_markers::read_markers(kind, data));
            add_notes(root, read);
            count = read.document.records.size();
            break;
        }
        case Kind::Ste: {
            const auto read = read_ste(data);
            if (!read.ok()) { reject = read.notes.empty() ? "" : read.notes.back().message; break; }
            header_props(read.document.header, read.document.records.size());
            plan(stage_markers::read_markers(kind, data));
            add_notes(root, read);
            count = read.document.records.size();
            break;
        }
        case Kind::Est: {
            const auto read = read_est(data);
            if (!read.ok()) { reject = read.notes.empty() ? "" : read.notes.back().message; break; }
            const auto& doc = read.document;
            header_props(doc.header, doc.rows.size());
            property(root, "Table at", hex(doc.table_offset), EvidenceLevel::StructuralConfirmed);
            property(root, "Programs", std::to_string(doc.programs.size()));
            for (std::size_t row = 0; row < doc.rows.size(); ++row) {
                InspectionNode node;
                node.id = "row-" + std::to_string(row);
                node.title = "Row " + std::to_string(row);
                node.kind = InspectionKind::Object;
                for (std::size_t mode = 0; mode < kEstModes; ++mode) {
                    const auto index = doc.rows[row][mode];
                    property(node, kEstModeNames[mode], index < 0 ? "none" : "program " + std::to_string(index));
                }
                root.children.push_back(std::move(node));
            }
            for (std::size_t p = 0; p < doc.programs.size(); ++p) {
                const auto& program = doc.programs[p];
                InspectionNode node;
                node.id = "program-" + std::to_string(p);
                node.title = "Program " + std::to_string(p) + " at " + hex(program.offset);
                node.kind = InspectionKind::Object;
                node.source_span = SourceSpan{program.offset, 0U};
                for (const auto& command : program.commands) {
                    std::string args;
                    for (const auto a : command.arguments) args += (args.empty() ? "" : ", ") + std::to_string(a);
                    property(node, "Command " + std::to_string(command.code), args.empty() ? "-" : args);
                }
                if (!program.terminated) property(node, "End", "no zero word before the table");
                root.children.push_back(std::move(node));
            }
            add_notes(root, read);
            out.image_preview = render_est(doc);
            count = doc.rows.size();
            break;
        }
        case Kind::Sef: {
            const auto read = read_sef(data);
            if (!read.ok()) { reject = read.notes.empty() ? "" : read.notes.back().message; break; }
            const auto& doc = read.document;
            property(root, "Sections declared", std::to_string(doc.sections_declared), EvidenceLevel::StructuralConfirmed);
            property(root, "Field at +6", std::to_string(doc.field6), EvidenceLevel::PreservedUndecoded);
            for (std::size_t i = 0; i < doc.sections.size(); ++i) {
                const auto& s = doc.sections[i];
                InspectionNode node;
                node.id = "section-" + std::to_string(i);
                node.title = "Section " + std::to_string(i);
                node.kind = InspectionKind::Object;
                node.source_span = SourceSpan{s.offset, s.size};
                property(node, "Offset", hex(s.offset));
                property(node, "Size", std::to_string(s.size) + " bytes");
                property(node, "Value", std::to_string(s.value) +
                                            (s.size == std::uint64_t{s.value} * 4U ? " (= size / 4: a count of words)"
                                                                                  : ""));
                root.children.push_back(std::move(node));
            }
            add_notes(root, read);
            out.image_preview = render_sef(doc, size);
            count = doc.sections.size();
            break;
        }
        }
        if (!reject.empty() || out.inspection.root.properties.empty()) {
            return module_support::reject(probe, module.id,
                                          std::string{"not a "} + kind_name(kind) + " member: " + reject);
        }
        out.detail = std::string{kind_name(kind)} + " | " + stage_markers::kind_meaning(kind) + " | " +
                     std::to_string(count) + " read";
        return out;
    } catch (...) {
        return module_support::reject(probe, module.id, "stage config module failed");
    }
}

[[nodiscard]] NativeModule make(const char* id, const char* family, Format format) noexcept {
    return {id, family, format, ModuleKind::Structural, false, run_stage_config,
            capability(ResourceCapability::Inspection) | ResourceCapability::ImagePreview};
}

}  // namespace

std::vector<NativeModule> stage_config_modules() noexcept {
    return {
        make("formats.stage-cfg.pos", "POS", Format::StagePos),
        make("formats.stage-cfg.eve", "EVE", Format::StageEve),
        make("formats.stage-cfg.cam", "CAM", Format::StageCam),
        make("formats.stage-cfg.itm", "ITM", Format::StageItm),
        make("formats.stage-cfg.ste", "STE", Format::StageSte),
        make("formats.stage-cfg.est", "EST", Format::StageEst),
        make("formats.stage-cfg.sef", "SEF", Format::StageSef),
    };
}

}  // namespace dmcresource
