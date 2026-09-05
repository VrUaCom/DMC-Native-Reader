#include "dmcresource/native_module.h"

#include <string_view>

namespace dmcresource {
namespace {

PipelineResult run_scm(std::string_view,
                       const std::uint8_t* bytes,
                       std::size_t size,
                       const ProbeResult& probe) noexcept {
    auto out = pipeline_from_decode(probe, decode_scm(bytes, size),
                                    "formats.scm.mesh-reader", true);
    if (out.accepted) {
        out.modules.insert(out.modules.begin() + 2,
                           {"model-family.mesh-core", true});
        out.modules.insert(out.modules.begin() + 3,
                           {"scm.scene-transform-adapter", true});
    }
    return out;
}

PipelineResult run_mod(std::string_view,
                       const std::uint8_t* bytes,
                       std::size_t size,
                       const ProbeResult& probe) noexcept {
    auto out = pipeline_from_decode(probe, decode_mod(bytes, size),
                                    "formats.mod.mesh-reader", true);
    if (out.accepted) {
        out.modules.insert(out.modules.begin() + 2,
                           {"model-family.mesh-core", true});
        out.modules.insert(out.modules.begin() + 3,
                           {"mod.skin-topology-adapter", true});
    }
    return out;
}

PipelineResult run_partial(std::string_view filename,
                           const std::uint8_t* bytes,
                           std::size_t size,
                           const ProbeResult& probe,
                           const char* module_id,
                           const char* pending_a,
                           const char* pending_b) noexcept {
    auto out = structural_pipeline(
        probe, module_id,
        describe_resource(filename, bytes, size, probe));
    out.modules.insert(out.modules.begin() + 2,
                       {"model-family.shared-envelope", true});
    out.modules.push_back({pending_a, false});
    out.modules.push_back({pending_b, false});
    return out;
}

PipelineResult run_efm(std::string_view filename,
                       const std::uint8_t* bytes,
                       std::size_t size,
                       const ProbeResult& probe) noexcept {
    return run_partial(filename, bytes, size, probe,
                       "formats.efm.family-adapter",
                       "efm.vertex-stream-binding",
                       "efm.material-topology-binding");
}

PipelineResult run_mrp(std::string_view filename,
                       const std::uint8_t* bytes,
                       std::size_t size,
                       const ProbeResult& probe) noexcept {
    return run_partial(filename, bytes, size, probe,
                       "formats.mrp.family-adapter",
                       "mrp.record-schema",
                       "mrp.downstream-owner-binding");
}

PipelineResult run_shw(std::string_view filename,
                       const std::uint8_t* bytes,
                       std::size_t size,
                       const ProbeResult& probe) noexcept {
    return run_partial(filename, bytes, size, probe,
                       "formats.shw.family-adapter",
                       "shw.triangle-index-binding",
                       "shw.external-spatial-pool-binding");
}

}  // namespace

NativeModule scm_module() noexcept {
    return {"formats.scm.mesh-reader", "SCM", Format::Scm,
            ModuleKind::Mesh, true, run_scm};
}
NativeModule mod_module() noexcept {
    return {"formats.mod.mesh-reader", "MOD", Format::Mod,
            ModuleKind::Mesh, true, run_mod};
}
NativeModule efm_module() noexcept {
    return {"formats.efm.family-adapter", "EFM", Format::Efm,
            ModuleKind::Partial, false, run_efm};
}
NativeModule mrp_module() noexcept {
    return {"formats.mrp.family-adapter", "MRP", Format::Mrp,
            ModuleKind::Partial, false, run_mrp};
}
NativeModule shw_module() noexcept {
    return {"formats.shw.family-adapter", "SHW", Format::Shw,
            ModuleKind::Partial, false, run_shw};
}

}  // namespace dmcresource
