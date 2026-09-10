#pragma once
#include "dmcresource/inspection_document.h"
namespace dmcresource {
struct Session;
enum class InspectionTopic : std::uint32_t { Uv = 1, Meshes = 2, Hierarchy = 3 };
[[nodiscard]] InspectionDocument inspect_session(const Session* session, InspectionTopic topic);
}  // namespace dmcresource
