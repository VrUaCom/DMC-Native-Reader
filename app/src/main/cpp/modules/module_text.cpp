#include "dmcresource/native_module.h"

#include "dmcresource/text_decode.h"

namespace dmcresource {
namespace {

PipelineResult run_stage_txt(const NativeModule& module,
                             std::string_view,
                             const std::uint8_t* bytes,
                             std::size_t size,
                             const ProbeResult& probe) noexcept {
    return pipeline_from_decode(probe, decode_stage_txt(bytes, size),
                                module.id, module.renderable);
}

PipelineResult run_index(const NativeModule& module,
                         std::string_view,
                         const std::uint8_t* bytes,
                         std::size_t size,
                         const ProbeResult& probe) noexcept {
    return pipeline_from_decode(probe, decode_index(bytes, size),
                                module.id, module.renderable);
}

}  // namespace

NativeModule stage_txt_module() noexcept {
    return {"formats.stage-txt.lexer", "TXT", Format::StageTxt,
            ModuleKind::Text, false, run_stage_txt};
}

NativeModule index_module() noexcept {
    return {"formats.index.manifest-reader", ".index", Format::Index,
            ModuleKind::Text, false, run_index};
}

}  // namespace dmcresource
