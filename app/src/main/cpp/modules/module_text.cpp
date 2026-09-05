#include "dmcresource/native_module.h"

#include "dmcresource/text_decode.h"

namespace dmcresource {
namespace {

PipelineResult run_stage_txt(std::string_view,
                             const std::uint8_t* bytes,
                             std::size_t size,
                             const ProbeResult& probe) noexcept {
    return pipeline_from_decode(probe, decode_stage_txt(bytes, size),
                                "formats.stage-txt.lexer", false);
}

PipelineResult run_index(std::string_view,
                         const std::uint8_t* bytes,
                         std::size_t size,
                         const ProbeResult& probe) noexcept {
    return pipeline_from_decode(probe, decode_index(bytes, size),
                                "formats.index.manifest-reader", false);
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
