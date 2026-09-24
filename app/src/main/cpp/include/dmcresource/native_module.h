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

// ModuleRun is the fail-closed ABI inside Native Reader. Implementations may
// call allocating helpers internally, but every ModuleRun entry point must catch
// all exceptions and return a minimal rejected PipelineResult.
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
    // Registry initialization uses std::vector and may allocate on first use;
    // run_decode_pipeline owns the outer fail-closed exception boundary.
    [[nodiscard]] static const NativeModule* find(std::string_view family);
    [[nodiscard]] static const std::vector<NativeModule>& modules();
};

// Allocating internal helper. Exceptions propagate to the owning ModuleRun
// boundary rather than being hidden behind a false noexcept promise.
[[nodiscard]] PipelineResult structural_pipeline(const ProbeResult& probe,
                                                 const char* module_id,
                                                 std::string detail);

// Native Reader routes geometry, texture and EVT inspection through portable
// C++23 product modules. DDS and PTX intentionally share one texture
// implementation; legacy .tm2 logical names route to that same validated
// wrapped-DDS path.
[[nodiscard]] NativeModule scm_module() noexcept;
[[nodiscard]] NativeModule mod_module() noexcept;
[[nodiscard]] NativeModule texture_module(Format format) noexcept;
[[nodiscard]] NativeModule evt_module() noexcept;
[[nodiscard]] NativeModule pac_module() noexcept;
[[nodiscard]] NativeModule mot_module() noexcept;
[[nodiscard]] NativeModule pnst_module() noexcept;
[[nodiscard]] NativeModule shw_module() noexcept;
[[nodiscard]] NativeModule tsc_module() noexcept;
[[nodiscard]] NativeModule clt_module() noexcept;
[[nodiscard]] NativeModule efm_module() noexcept;
[[nodiscard]] NativeModule motion_script_module() noexcept;
[[nodiscard]] NativeModule colshape_module() noexcept;
[[nodiscard]] NativeModule colindex_module() noexcept;

}  // namespace dmcresource
