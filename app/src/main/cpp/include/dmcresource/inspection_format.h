#pragma once

#include <string>

#include "dmcresource/inspection_document.h"

namespace dmcresource {

[[nodiscard]] std::size_t count_inspection_nodes(
    const InspectionNode& node, InspectionKind kind) noexcept;

[[nodiscard]] std::string format_inspection_tree(const InspectionDocument& document);

}  // namespace dmcresource
