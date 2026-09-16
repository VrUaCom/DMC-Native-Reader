#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "dmcresource/decode_pipeline.h"
#include "dmcresource/dmc_resource.h"
#include "dmcresource/resource_capabilities.h"

namespace dmcresource {

enum class ModuleKind : std::uint8_t {
    Mesh,
    Structural,
};

struct NativeModule;

using ModuleRun = PipelineResult (*)(const NativeModule& module,
                                     std::string_view filename,
                                     const std::uint8_t* bytes,
                                     std::size_t size,
                                     const ProbeResult& probe) noexcept;

struct NativeModule final {
    const char* id;
    const char* family;
    Format format;
    ModuleKind kind;
    bool renderable;
    ModuleRun run;
    ResourceCapabilities capabilities{};
};

class NativeModuleRegistry final {
public:
    [[nodiscard]] static const NativeModule* find(std::string_view family) noexcept;
    [[nodiscard]] static const std::vector<NativeModule>& modules() noexcept;
};

[[nodiscard]] PipelineResult structural_pipeline(const ProbeResult& probe,
                                                 const char* module_id,
                                                 std::string detail) noexcept;

// Native Reader routes geometry, texture and EVT inspection through portable
// C++23 product modules. DDS and PTX intentionally share one texture
// implementation; legacy .tm2 logical names route to that same validated
// wrapped-DDS path.
[[nodiscard]] NativeModule scm_module() noexcept;
[[nodiscard]] NativeModule mod_module() noexcept;
[[nodiscard]] NativeModule texture_module(Format format) noexcept;
[[nodiscard]] NativeModule evt_module() noexcept;

}  // namespace dmcresource
