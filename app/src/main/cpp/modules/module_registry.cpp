#include "dmcresource/native_module.h"

#include <algorithm>
#include <string_view>
#include <utility>

namespace dmcresource {

const std::vector<NativeModule>& NativeModuleRegistry::modules() noexcept {
    static const std::vector<NativeModule> registry{
        scm_module(),
        mod_module(),
        dds_module(),
        ptx_module(),
    };
    return registry;
}

const NativeModule* NativeModuleRegistry::find(std::string_view family) noexcept {
    const auto& registry = modules();
    const auto it = std::find_if(
        registry.begin(), registry.end(),
        [family](const NativeModule& module) {
            return std::string_view{module.family} == family;
        });
    return it == registry.end() ? nullptr : &*it;
}

PipelineResult structural_pipeline(const ProbeResult& probe,
                                   const char* module_id,
                                   std::string detail) noexcept {
    PipelineResult out;
    out.accepted = true;
    out.renderable = false;
    out.probe = probe;
    out.detail = std::move(detail);
    out.modules.push_back({"identity-probe", true});
    out.modules.push_back({"bounded-read-guard", true});
    out.modules.push_back({module_id, true});
    return out;
}

}  // namespace dmcresource
