#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "dmcresource/decode.h"
#include "dmcresource/decode_pipeline.h"
#include "dmcresource/dmc_resource.h"

namespace dmcresource {

enum class ModuleKind : std::uint8_t {
    Mesh,
    Text,
    Structural,
    Container,
    Partial,
};

using ModuleRun = PipelineResult (*)(std::string_view filename,
                                     const std::uint8_t* bytes,
                                     std::size_t size,
                                     const ProbeResult& probe) noexcept;

struct NativeModule final {
    const char* id;
    const char* family;
    ModuleKind kind;
    bool renderable;
    ModuleRun run;
};

class NativeModuleRegistry final {
public:
    [[nodiscard]] static const NativeModule* find(std::string_view family) noexcept;
    [[nodiscard]] static const std::vector<NativeModule>& modules() noexcept;
};

// Shared helpers used by independent module translation units.
[[nodiscard]] PipelineResult pipeline_from_decode(const ProbeResult& probe,
                                                  DecodeResult decoded,
                                                  const char* module_id,
                                                  bool renderable) noexcept;
[[nodiscard]] PipelineResult structural_pipeline(const ProbeResult& probe,
                                                 const char* module_id,
                                                 std::string detail) noexcept;

// Module factories. Keeping one factory per translation unit makes the registry
// composable and prevents a central format switch from accumulating again.
[[nodiscard]] NativeModule scm_module() noexcept;
[[nodiscard]] NativeModule mod_module() noexcept;
[[nodiscard]] NativeModule hits_module() noexcept;
[[nodiscard]] NativeModule stage_txt_module() noexcept;
[[nodiscard]] NativeModule index_module() noexcept;
[[nodiscard]] NativeModule dds_module() noexcept;
[[nodiscard]] NativeModule ptx_module() noexcept;
[[nodiscard]] NativeModule dca_module() noexcept;
[[nodiscard]] NativeModule lig_module() noexcept;
[[nodiscard]] NativeModule lig2_module() noexcept;
[[nodiscard]] NativeModule pac_module() noexcept;
[[nodiscard]] NativeModule pnst_module() noexcept;
[[nodiscard]] NativeModule nbz_module() noexcept;
[[nodiscard]] NativeModule efm_module() noexcept;
[[nodiscard]] NativeModule mrp_module() noexcept;
[[nodiscard]] NativeModule shw_module() noexcept;
[[nodiscard]] NativeModule generic_module() noexcept;

}  // namespace dmcresource
