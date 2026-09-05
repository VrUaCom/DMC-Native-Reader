#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "dmcresource/dmc_resource.h"
#include "dmcresource/mesh.h"

namespace dmcresource {

struct ModuleState {
    const char* name;
    bool complete;
};

struct PipelineResult {
    bool accepted{false};
    bool renderable{false};
    ProbeResult probe;
    Mesh mesh;
    std::string detail;
    std::vector<ModuleState> modules;
};

// Composable decoder entry point used by the v9 test reader.  It reuses the
// existing corpus-backed MOD/SCM decoder as the shared geometry module and
// exposes explicit family adapters for EFM/MRP/SHW.  Partial adapters never
// fabricate geometry: incomplete modules are reported as pending.
PipelineResult run_decode_pipeline(std::string_view filename,
                                   const std::uint8_t* bytes,
                                   std::size_t size) noexcept;

std::string pipeline_trace(const PipelineResult& result);

}  // namespace dmcresource
