#pragma once

#include <cstddef>
#include <cstdint>

#include "dmcresource/decode_pipeline.h"

namespace dmcresource::adapters {

// Canonical SCM path: parse exactly once with the pinned dmc-rengine-cpp
// structural parser, then project typed geometry/material/hierarchy data into
// Native Reader v2 IR. The legacy flattened Mesh is produced only as a
// compatibility projection for the current UI renderer.
[[nodiscard]] PipelineResult run_scm_adapter(const ProbeResult& probe,
                                             const std::uint8_t* bytes,
                                             std::size_t size,
                                             const char* module_id) noexcept;

}  // namespace dmcresource::adapters
