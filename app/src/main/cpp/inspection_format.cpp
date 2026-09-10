#include "dmcresource/inspection_format.h"

#include <cstddef>
#include <sstream>
#include <string_view>

namespace dmcresource {
namespace {

const char* evidence_name(EvidenceLevel evidence) noexcept {
    switch (evidence) {
    case EvidenceLevel::Unknown: return "UNKNOWN";
    case EvidenceLevel::Recognized: return "RECOGNIZED";
    case EvidenceLevel::StructuralConfirmed: return "STRUCTURAL_CONFIRMED";
    case EvidenceLevel::DataConfirmed: return "DATA_CONFIRMED";
    case EvidenceLevel::ExeConfirmed: return "EXE_CONFIRMED";
    case EvidenceLevel::ExeAndCorpusConfirmed: return "EXE_AND_CORPUS_CONFIRMED";
    case EvidenceLevel::PreservedUndecoded: return "PRESERVED_UNDECODED";
    }
    return "UNKNOWN";
}

void append_node(std::ostringstream& out,
                 const InspectionNode& node,
                 std::size_t depth) {
    const std::string indent(depth * 2U, ' ');
    out << indent << node.title;
    if (node.source_span.has_value()) {
        out << "  @0x" << std::hex << node.source_span->offset
            << "+0x" << node.source_span->size << std::dec;
    }
    out << '\n';

    for (const auto& property : node.properties) {
        out << indent << "  " << property.key << ": " << property.value;
        if (property.evidence != EvidenceLevel::Unknown) {
            out << "  [" << evidence_name(property.evidence) << "]";
        }
        out << '\n';
    }

    for (const auto& child : node.children) {
        append_node(out, child, depth + 1U);
    }
}

}  // namespace

std::size_t count_inspection_nodes(const InspectionNode& node, InspectionKind kind) noexcept {
    std::size_t count = node.kind == kind ? 1U : 0U;
    for (const auto& child : node.children) count += count_inspection_nodes(child, kind);
    return count;
}

std::string format_inspection_tree(const InspectionDocument& document) {
    if (document.empty()) return {};

    std::ostringstream out;
    if (!document.format.empty()) {
        out << "Format: " << document.format << '\n';
    }
    append_node(out, document.root, 0U);
    return out.str();
}

}  // namespace dmcresource
