#include "dmcresource/native_module.h"

#include <cstdio>
#include <map>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "dmcresource/decode_pipeline.h"
#include "dmcresource/font.h"
#include "dmcresource/module_support.h"

namespace dmcresource {
namespace {

// The sheet: 32 cells across, within this many pixels.
constexpr std::uint32_t kSheetColumns = 32U;
constexpr std::uint64_t kSheetMaxPixels = 8U * 1024U * 1024U;

PipelineResult run_font_module(const NativeModule& module,
                               std::string_view,
                               const std::uint8_t* bytes,
                               std::size_t size,
                               const ProbeResult& probe) noexcept {
    try {
        const std::span<const std::uint8_t> data{bytes, size};
        const auto read = font::read(data);
        if (!read.ok()) return module_support::reject(probe, module.id, "not a FON font: " + read.error);
        const auto& f = *read.font;
        PipelineResult out;
        out.accepted = true;
        out.renderable = false;
        out.probe = probe;
        out.modules.push_back({"identity-probe", true});
        out.modules.push_back({"bounded-read-guard", true});
        out.modules.push_back({module.id, true});
        out.inspection.format = "FON";
        out.inspection.root.id = "fon";
        out.inspection.root.title = "Bitmap font";
        out.inspection.root.kind = InspectionKind::Document;
        out.inspection.root.source_span = SourceSpan{0U, size};
        const auto add = [&out](const char* key, std::string value, EvidenceLevel e) {
            out.inspection.root.properties.push_back({key, std::move(value), e});
        };
        add("Glyphs", std::to_string(f.glyph_count), EvidenceLevel::StructuralConfirmed);
        add("Cell", std::to_string(f.width) + " x " + std::to_string(f.height) + " px, 1 bit (size inferred, not stored)",
            EvidenceLevel::DataConfirmed);
        add("Glyph bytes", std::to_string(f.glyph_bytes) + " (" + std::to_string(f.row_bytes) + " per row)",
            EvidenceLevel::StructuralConfirmed);
        add("Pages", std::to_string(f.pages) + " of 256 characters by UTF-16 high byte",
            EvidenceLevel::StructuralConfirmed);

        // Coverage by Unicode block.
        std::vector<std::pair<std::string, std::uint32_t>> blocks;
        for (const auto& c : f.characters) {
            const std::string name = font::block_name(static_cast<std::uint8_t>(c.code >> 8U));
            if (blocks.empty() || blocks.back().first != name) {
                bool found = false;
                for (auto& [existing, count] : blocks) {
                    if (existing == name) {
                        ++count;
                        found = true;
                        break;
                    }
                }
                if (!found) blocks.emplace_back(name, 1U);
            } else {
                ++blocks.back().second;
            }
        }
        InspectionNode coverage;
        coverage.id = "fon-coverage";
        coverage.title = "Characters by block";
        coverage.kind = InspectionKind::Collection;
        for (const auto& [name, count] : blocks) {
            coverage.properties.push_back({name, std::to_string(count), EvidenceLevel::DataConfirmed});
        }
        out.inspection.root.children.push_back(std::move(coverage));

        InspectionNode first;
        first.id = "fon-first";
        first.title = "First characters (ink measured)";
        first.kind = InspectionKind::Collection;
        for (std::size_t index = 0U; index < f.characters.size() && index < 16U; ++index) {
            const auto& c = f.characters[index];
            char code[12];
            std::snprintf(code, sizeof code, "U+%04X", c.code);
            const auto ink = font::ink_span(f, data, c.number);
            first.properties.push_back(
                {code,
                 "glyph " + std::to_string(c.number) + ", ink " +
                     (ink ? std::to_string(ink->first) + ".." + std::to_string(ink->last) : std::string{"none"}),
                 EvidenceLevel::DataConfirmed});
        }
        out.inspection.root.children.push_back(std::move(first));

        auto sheet = font::render_sheet(f, data, kSheetColumns, kSheetMaxPixels);
        out.image_preview.width = sheet.width;
        out.image_preview.height = sheet.height;
        out.image_preview.rgba8 = std::move(sheet.rgba8);
        std::ostringstream detail;
        detail << "FON | glyphs=" << f.glyph_count << " cell=" << f.width << "x" << f.height << " pages=" << f.pages
               << " sheet=" << sheet.shown << "/" << f.glyph_count;
        out.detail = detail.str();
        return out;
    } catch (...) {
        return module_support::reject(probe, module.id, "font module failed");
    }
}

}  // namespace

NativeModule font_module() noexcept {
    return {"formats.fon.reader", "FON", Format::Fon, ModuleKind::Structural, false, run_font_module,
            capability(ResourceCapability::Inspection) | ResourceCapability::ImagePreview};
}

}  // namespace dmcresource
