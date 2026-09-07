#include "dmcresource/native_module.h"

#include <string_view>

#include "dmcresource/adapters/mod_adapter.h"
#include "dmcresource/adapters/scm_adapter.h"

namespace dmcresource {
namespace {

PipelineResult run_scm(const NativeModule& module,
                       std::string_view,
                       const std::uint8_t* bytes,
                       std::size_t size,
                       const ProbeResult& probe) noexcept {
    return adapters::run_scm_adapter(probe, bytes, size, module.id);
}

PipelineResult run_mod(const NativeModule& module,
                       std::string_view,
                       const std::uint8_t* bytes,
                       std::size_t size,
                       const ProbeResult& probe) noexcept {
    return adapters::run_mod_adapter(probe, bytes, size, module.id);
}

PipelineResult run_partial(const NativeModule& module,
                           std::string_view filename,
                           const std::uint8_t* bytes,
                           std::size_t size,
                           const ProbeResult& probe,
                           const char* pending_a,
                           const char* pending_b) noexcept {
    auto out = structural_pipeline(
        probe, module.id,
        describe_resource(filename, bytes, size, probe));
    out.modules.insert(out.modules.begin() + 2,
                       {"model-family.shared-envelope", true});
    out.modules.push_back({pending_a, false});
    out.modules.push_back({pending_b, false});
    return out;
}

PipelineResult run_efm(const NativeModule& module,
                       std::string_view filename,
                       const std::uint8_t* bytes,
                       std::size_t size,
                       const ProbeResult& probe) noexcept {
    return run_partial(module, filename, bytes, size, probe,
                       "efm.vertex-stream-binding",
                       "efm.material-topology-binding");
}

PipelineResult run_mrp(const NativeModule& module,
                       std::string_view filename,
                       const std::uint8_t* bytes,
                       std::size_t size,
                       const ProbeResult& probe) noexcept {
    return run_partial(module, filename, bytes, size, probe,
                       "mrp.record-schema",
                       "mrp.downstream-owner-binding");
}

PipelineResult run_shw(const NativeModule& module,
                       std::string_view filename,
                       const std::uint8_t* bytes,
                       std::size_t size,
                       const ProbeResult& probe) noexcept {
    return run_partial(module, filename, bytes, size, probe,
                       "shw.typed-reader-sync",
                       "shw.render-scene-adapter");
}

}  // namespace

NativeModule scm_module() noexcept {
    const auto caps = capability(ResourceCapability::Inspection) |
        ResourceCapability::Geometry |
        ResourceCapability::Wireframe |
        ResourceCapability::NodeHierarchy |
        ResourceCapability::TextureBinding;
    return {"formats.scm.mesh-reader", "SCM", Format::Scm,
            ModuleKind::Mesh, true, run_scm, caps};
}
NativeModule mod_module() noexcept {
    const auto caps = capability(ResourceCapability::Inspection) |
        ResourceCapability::Geometry |
        ResourceCapability::Wireframe |
        ResourceCapability::NodeHierarchy |
        ResourceCapability::SkeletalSkinning |
        ResourceCapability::SkinWeights |
        ResourceCapability::TextureBinding;
    return {"formats.mod.mesh-reader", "MOD", Format::Mod,
            ModuleKind::Mesh, true, run_mod, caps};
}
NativeModule efm_module() noexcept {
    return {"formats.efm.family-adapter", "EFM", Format::Efm,
            ModuleKind::Partial, false, run_efm,
            capability(ResourceCapability::Inspection)};
}
NativeModule mrp_module() noexcept {
    return {"formats.mrp.family-adapter", "MRP", Format::Mrp,
            ModuleKind::Partial, false, run_mrp,
            capability(ResourceCapability::Inspection)};
}
NativeModule shw_module() noexcept {
    return {"formats.shw.family-adapter", "SHW", Format::Shw,
            ModuleKind::Partial, false, run_shw,
            capability(ResourceCapability::Inspection)};
}

}  // namespace dmcresource
