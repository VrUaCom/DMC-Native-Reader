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

}  // namespace dmcresource
