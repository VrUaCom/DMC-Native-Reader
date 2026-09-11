#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "dmcresource/dmc_resource.h"
#include "dmcresource/image_preview.h"
#include "dmcresource/inspection_document.h"
#include "dmcresource/render_scene.h"
#include "dmcresource/resource_capabilities.h"

namespace dmcresource {

// Generic nested-resource projection used by container/bundle modules.
// Android never needs to know whether the parent is PTX/PAC/PNST/etc.
// A child may expose inspection, a static image, geometry, and more children
// through the same Architecture v2 contracts as a top-level resource.
struct ChildResource {
    std::string id;
    std::string title;
    std::string suggested_filename;
    SourceSpan source_span{};
    ProbeResult probe{};
    ResourceCapabilities capabilities{};
    InspectionDocument inspection;
    RenderScene scene;
    ImagePreview image_preview;
    std::vector<ChildResource> children;

    // Optional bounded source payload for lazy child materialization. Containers
    // use this only when a child cannot keep a decoded preview resident (for
    // example a PTX slot beyond the gallery RGBA memory budget). The parent
    // retains compressed/encoded bytes; opening/exporting the child routes them
    // back through the canonical decoder rather than inventing a second codec.
    std::vector<std::uint8_t> source_bytes;

    std::string detail;
    std::string trace;
    bool renderable{false};
};

}  // namespace dmcresource
