#include "dmcresource/native_module.h"

#include "dmcresource/hits_decode.h"

namespace dmcresource {
namespace {

PipelineResult run_hits(const NativeModule& module,
                        std::string_view,
                        const std::uint8_t* bytes,
                        std::size_t size,
                        const ProbeResult& probe) noexcept {
    return pipeline_from_decode(probe, decode_hits(bytes, size),
                                module.id, module.renderable);
}

}  // namespace

NativeModule hits_module() noexcept {
    return {"formats.hits.collision-reader", "HITS", Format::Hits,
            ModuleKind::Mesh, true, run_hits};
}

}  // namespace dmcresource
