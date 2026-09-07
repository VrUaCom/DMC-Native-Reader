#include "dmcresource/portable_session.h"

#include <algorithm>
#include <exception>
#include <iomanip>
#include <sstream>
#include <string>
#include <utility>

namespace dmcresource {
namespace {

const char* evidence_name(EvidenceLevel level) noexcept {
    switch (level) {
    case EvidenceLevel::Recognized: return "RECOGNIZED";
    case EvidenceLevel::StructuralConfirmed: return "STRUCTURAL_CONFIRMED";
    case EvidenceLevel::DataConfirmed: return "DATA_CONFIRMED";
    case EvidenceLevel::ExeConfirmed: return "EXE_CONFIRMED";
    case EvidenceLevel::ExeAndCorpusConfirmed: return "EXE_AND_CORPUS_CONFIRMED";
    case EvidenceLevel::PreservedUndecoded: return "PRESERVED_UNDECODED";
    case EvidenceLevel::Unknown: return "UNKNOWN";
    }
    return "UNKNOWN";
}

void append_node(const InspectionNode& node,
                 std::size_t depth,
                 std::size_t max_lines,
                 std::size_t* line_count,
                 std::ostringstream* out) {
    if (out == nullptr || line_count == nullptr || *line_count >= max_lines) return;

    const std::string indent(depth * 2U, ' ');
    if (!node.title.empty()) {
        *out << indent << node.title;
        if (node.source_span.has_value()) {
            *out << " @0x" << std::hex << node.source_span->offset
                 << "+0x" << node.source_span->size << std::dec;
        }
        *out << '\n';
        ++(*line_count);
    }

    for (const auto& property : node.properties) {
        if (*line_count >= max_lines) return;
        *out << indent << "  " << property.key << ": " << property.value
             << " [" << evidence_name(property.evidence) << "]\n";
        ++(*line_count);
    }

    for (const auto& child : node.children) {
        append_node(child, depth + 1U, max_lines, line_count, out);
        if (*line_count >= max_lines) return;
    }
}

}  // namespace

std::unique_ptr<PortableSession> PortableSession::decode(std::string_view filename,
                                                         const std::uint8_t* bytes,
                                                         std::size_t size,
                                                         std::string* error) {
    try {
        auto result = run_decode_pipeline(filename, bytes, size);
        if (!result.accepted) {
            if (error != nullptr) {
                *error = result.detail.empty() ? "resource rejected by Architecture v2 pipeline"
                                               : result.detail;
            }
            return nullptr;
        }
        return std::unique_ptr<PortableSession>(new PortableSession(std::move(result)));
    } catch (const std::exception& ex) {
        if (error != nullptr) *error = ex.what();
        return nullptr;
    } catch (...) {
        if (error != nullptr) *error = "unknown native decode failure";
        return nullptr;
    }
}

PortableSession::PortableSession(PipelineResult&& result)
    : result_(std::move(result)) {
    if (result_.renderable && result_.scene.has_geometry()) {
        geometry_ready_ = materialize_render_scene(result_.scene, &render_mesh_);
        if (geometry_ready_) {
            // Materialize once. The overlay remains unavailable unless every
            // participating node has canonical spatial authority.
            (void)materialize_hierarchy_overlay(result_.scene, &hierarchy_);
        }
    }
}

const ChildResource* PortableSession::child(std::size_t index) const noexcept {
    return index < result_.children.size() ? &result_.children[index] : nullptr;
}

RgbaImage PortableSession::render(int width,
                                  int height,
                                  const ViewState& view,
                                  RenderFlags flags) const {
    if (!geometry_ready_) return {};
    const int safe_width = std::clamp(width, 64, 2048);
    const int safe_height = std::clamp(height, 64, 2048);

    ViewState effective_view = view;
    if (has_render_flag(flags, RenderFlag::Wireframe)) {
        effective_view.wireframe = true;
    }

    const HierarchyOverlay* overlay = nullptr;
    if (has_render_flag(flags, RenderFlag::Hierarchy) && hierarchy_available()) {
        overlay = &hierarchy_;
    }

    return render_view(render_mesh_, safe_width, safe_height, effective_view, overlay);
}

std::string PortableSession::summary() const {
    std::ostringstream out;
    const char* family = result_.probe.family != nullptr ? result_.probe.family : "UNKNOWN";
    out << family;
    if (geometry_ready_) {
        out << " | vertices=" << render_mesh_.vertices.size()
            << " | triangles=" << (render_mesh_.indices.size() / 3U)
            << " | nodes=" << result_.scene.nodes.size();
        if (hierarchy_available()) out << " | hierarchy=spatial";
    }
    if (result_.image_preview.available()) {
        out << " | image=" << result_.image_preview.width << 'x'
            << result_.image_preview.height;
    }
    if (!result_.children.empty()) {
        out << " | children=" << result_.children.size();
    }
    if (!result_.detail.empty()) out << " | " << result_.detail;
    return out.str();
}

std::string PortableSession::inspection_text(std::size_t max_lines) const {
    if (max_lines == 0U) return {};
    std::ostringstream out;
    if (!result_.inspection.format.empty()) {
        out << "Format: " << result_.inspection.format << '\n';
    }
    std::size_t line_count = result_.inspection.format.empty() ? 0U : 1U;
    append_node(result_.inspection.root, 0U, max_lines, &line_count, &out);
    if (line_count >= max_lines) out << "... inspection truncated ...\n";
    return out.str();
}

}  // namespace dmcresource
