#pragma once

#include <cstddef>
#include <cstdint>

#include "dmcresource/decode_pipeline.h"

namespace dmcresource::adapters {

// Canonical MOD path: parse exactly once with the pinned dmc-rengine-cpp
// structural parser, then project typed data into Native Reader v2 IR.
[[nodiscard]] PipelineResult run_mod_adapter(const ProbeResult& probe,
                                             const std::uint8_t* bytes,
                                             std::size_t size,
                                             const char* module_id) noexcept;

}  // namespace dmcresource::adapters
