#include "dmcresource/decode_pipeline.h"

#include <sstream>
#include <string_view>

#include "dmcresource/native_module.h"

namespace dmcresource {

PipelineResult run_decode_pipeline(std::string_view filename,
                                   const std::uint8_t* bytes,
                                   std::size_t size) noexcept {
    PipelineResult rejected;
    rejected.probe = probe(filename, bytes, size);
    if (!rejected.probe.recognized) {
        rejected.detail = "pipeline rejected unknown resource";
        return rejected;
    }

    const auto* module = NativeModuleRegistry::find(
        rejected.probe.family != nullptr
            ? std::string_view{rejected.probe.family}
            : std::string_view{});
    if (module == nullptr || module->run == nullptr) {
        rejected.detail = "recognized resource has no registered native module";
        return rejected;
    }
    return module->run(filename, bytes, size, rejected.probe);
}

std::string pipeline_trace(const PipelineResult& result) {
    std::ostringstream out;
    out << "modules:";
    for (const auto& module : result.modules) {
        out << "\n  " << (module.complete ? "[OK] " : "[TODO] ") << module.name;
    }
    return out.str();
}

}  // namespace dmcresource
