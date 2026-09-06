#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "dmcresource/dmc_resource.h"
#include "dmcresource/inspection_document.h"
#include "dmcresource/mesh.h"
#include "dmcresource/render_scene.h"
#include "dmcresource/resource_capabilities.h"

namespace dmcresource {

struct ModuleState {
    const char* name;
    bool complete;
};

struct PipelineResult {
    bool accepted{false};
    bool renderable{false};
    ProbeResult probe;

    // v1 compatibility projection. Format adapters are migrated toward
    // RenderScene incrementally; Android rendering remains stable meanwhile.
    Mesh mesh;

    // v2 reusable contracts. Parsers stay format-specific; adapters publish
    // generic inspection/render projections without reparsing the source.
    ResourceCapabilities capabilities{};
    InspectionDocument inspection;
    RenderScene scene;

    std::string detail;
    std::vector<ModuleState> modules;
};

// Registry-driven decoder entry point. The registry owns format authority and
// module capabilities; individual format modules own parsing/inspection.
PipelineResult run_decode_pipeline(std::string_view filename,
                                   const std::uint8_t* bytes,
                                   std::size_t size) noexcept;

std::string pipeline_trace(const PipelineResult& result);

}  // namespace dmcresource
