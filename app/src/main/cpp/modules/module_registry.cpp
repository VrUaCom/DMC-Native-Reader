#include "dmcresource/native_module.h"

#include <algorithm>
#include <string_view>
#include <utility>

namespace dmcresource {

const std::vector<NativeModule>& NativeModuleRegistry::modules() noexcept {
    static const std::vector<NativeModule> registry{
        scm_module(),
        mod_module(),
        hits_module(),
        stage_txt_module(),
        index_module(),
        dds_module(),
        ptx_module(),
        dca_module(),
        lig_module(),
        lig2_module(),
        pac_module(),
        pnst_module(),
        nbz_module(),
        efm_module(),
        mrp_module(),
        shw_module(),
        generic_module(),
    };
    return registry;
}

const NativeModule* NativeModuleRegistry::find(std::string_view family) noexcept {
    const auto& registry = modules();
    const auto it = std::find_if(registry.begin(), registry.end(),
                                 [family](const NativeModule& module) {
                                     return std::string_view{module.family} == family;
                                 });
    if (it != registry.end()) return &*it;

    const auto fallback = std::find_if(registry.begin(), registry.end(),
                                       [](const NativeModule& module) {
                                           return std::string_view{module.family} == "*";
                                       });
    return fallback == registry.end() ? nullptr : &*fallback;
}

PipelineResult pipeline_from_decode(const ProbeResult& probe,
                                    DecodeResult decoded,
                                    const char* module_id,
                                    bool renderable) noexcept {
    PipelineResult out;
    out.probe = probe;
    out.accepted = decoded.status == DecodeStatus::Ok;
    out.renderable = out.accepted && renderable &&
                     !decoded.mesh.vertices.empty() &&
                     !decoded.mesh.indices.empty();
    out.mesh = std::move(decoded.mesh);
    out.modules.push_back({"identity-probe", true});
    out.modules.push_back({"bounded-read-guard", true});
    out.modules.push_back({module_id, out.accepted});
    out.detail = decoded.detail != nullptr ? decoded.detail : "module completed";
    if (!decoded.info.empty()) {
        if (!out.detail.empty()) out.detail += "\n";
        out.detail += decoded.info;
    }
    if (!decoded.text.empty()) {
        if (!out.detail.empty()) out.detail += "\n";
        out.detail += decoded.text;
    }
    return out;
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
