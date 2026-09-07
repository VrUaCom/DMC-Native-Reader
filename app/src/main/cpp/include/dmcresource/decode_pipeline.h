#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "dmcresource/dmc_resource.h"
#include "dmcresource/image_preview.h"
#include "dmcresource/inspection_document.h"
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

    // Architecture v2 reusable contracts. Format-specific parsers/adapters may
    // use temporary local Mesh values internally, but geometry leaves a module
    // only through RenderScene. Static image resources use ImagePreview rather
    // than a second geometry representation or a format-specific Android path.
    ResourceCapabilities capabilities{};
    InspectionDocument inspection;
    RenderScene scene;
    ImagePreview image_preview;

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
