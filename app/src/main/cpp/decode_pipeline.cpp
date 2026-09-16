#include "dmcresource/decode_pipeline.h"

#include <sstream>
#include <string>
#include <string_view>

#include "dmcresource/native_module.h"

namespace dmcresource {
namespace {

void enforce_render_scene_contract(PipelineResult& result) {
    if (!result.renderable) return;

    const bool complete = result.scene.has_geometry();
    result.modules.push_back({"render-scene-contract", complete});
    if (complete) return;

    result.renderable = false;
    if (!result.detail.empty()) result.detail += "\n";
    result.detail +=
        "render contract rejected: renderable module did not publish RenderScene geometry";
}

void ensure_minimal_inspection(PipelineResult& result) {
    if (!result.inspection.empty()) return;

    const std::string family = result.probe.family != nullptr
        ? std::string{result.probe.family}
        : std::string{"Unknown"};
    result.inspection.format = family;
    result.inspection.root.id = "resource";
    result.inspection.root.title = family;
    result.inspection.root.kind = InspectionKind::Document;
    if (!result.detail.empty()) {
        result.inspection.root.properties.push_back({
            "Summary",
            result.detail,
            result.accepted ? EvidenceLevel::Recognized : EvidenceLevel::Unknown,
        });
    }
}

}  // namespace

PipelineResult run_decode_pipeline(std::string_view filename,
                                   const std::uint8_t* bytes,
                                   std::size_t size) noexcept {
    // This is the portable Core exception boundary. Probing, registry first-use,
    // diagnostics and typed IR projection may allocate internally; none of those
    // exceptions are allowed to escape into Session/JNI callers.
    PipelineResult rejected;
    try {
        rejected.probe = probe(filename, bytes, size);

        if (!rejected.probe.recognized) {
            rejected.detail = "pipeline rejected: format is outside the Native Reader 1.0 core";
            return rejected;
        }

        const auto* module = NativeModuleRegistry::find(
            rejected.probe.family != nullptr
                ? std::string_view{rejected.probe.family}
                : std::string_view{});
        if (module == nullptr || module->run == nullptr) {
            rejected.detail = "recognized core resource has no registered native module";
            return rejected;
        }

        auto authoritative_probe = rejected.probe;
        authoritative_probe.format = module->format;

        auto result = module->run(*module, filename, bytes, size, authoritative_probe);
        result.capabilities = module->capabilities;
        enforce_render_scene_contract(result);
        ensure_minimal_inspection(result);
        return result;
    } catch (...) {
        // Default/partially populated PipelineResult owns only already-created
        // storage. Returning it moves that storage and requires no new diagnostic
        // allocation, so allocation failure remains a normal rejected pipeline.
        rejected.accepted = false;
        rejected.renderable = false;
        return rejected;
    }
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
