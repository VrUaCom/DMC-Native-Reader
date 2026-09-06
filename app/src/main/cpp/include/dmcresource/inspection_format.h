#pragma once

#include <string>

#include "dmcresource/inspection_document.h"

namespace dmcresource {

[[nodiscard]] std::string format_inspection_tree(const InspectionDocument& document);

}  // namespace dmcresource
