#include "dmcresource/decode_pipeline.h"

#include <sstream>
#include <string>
#include <string_view>
#include <utility>

#include "dmcresource/native_module.h"

namespace dmcresource {
namespace {

[[nodiscard]] bool has_index_extension(std::string_view filename) noexcept {
    constexpr std::string_view extension = ".index";
    if (filename.size() < extension.size()) return false;
    const auto tail = filename.substr(filename.size() - extension.size());
    for (std::size_t i = 0; i < extension.size(); ++i) {
        char value = tail[i];
        if (value >= 'A' && value <= 'Z') {
            value = static_cast<char>(value - 'A' + 'a');
        }
        if (value != extension[i]) return false;
    }
    return true;
}

void project_legacy_mesh_once(PipelineResult& result) {
    if (!result.renderable || !result.scene.meshes.empty() ||
        result.mesh.vertices.empty() || result.mesh.indices.empty()) {
        return;
    }

    MeshPrimitive primitive;
    primitive.name = result.probe.family != nullptr
        ? std::string{result.probe.family}
        : std::string{"resource"};
    primitive.mesh = result.mesh;
    result.scene.meshes.push_back(std::move(primitive));
}

void enforce_render_scene_contract(PipelineResult& result) {
    if (!result.renderable) return;

    const bool complete = result.scene.has_geometry();
    result.modules.push_back({"render-scene-contract", complete});
    if (complete) return;

    // From Architecture v2 onward, JNI/render consumers have exactly one
    // geometry contract. A module may still internally produce the v1 Mesh,
    // but the pipeline must project it into RenderScene before advertising a
    // usable preview. Never revive a second rendering path as a fallback.
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
    PipelineResult rejected;

    // DMC3 .index files are textual extraction/naming metadata and may begin
    // with the literal line `PNST` or `PAC`. That four-byte text prefix must
    // not be promoted to binary-container authority. Force extension/name
    // identity for this metadata family, then let its own module validate text.
    rejected.probe = has_index_extension(filename)
        ? probe(filename, nullptr, 0u)
        : probe(filename, bytes, size);

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

    // The catalog owns recognition/evidence metadata; once a family has a
    // registered module contract, the registry owns its Native Reader format
    // identity and execution policy. There is no wildcard/fallback dispatcher.
    auto authoritative_probe = rejected.probe;
    if (module->format != Format::Unknown) {
        authoritative_probe.format = module->format;
    }

    auto result = module->run(*module, filename, bytes, size, authoritative_probe);
    result.capabilities = module->capabilities;

    // Transitional v2 adapter: existing renderable modules may still publish
    // the v1 flattened Mesh internally. Project it exactly once into the
    // reusable RenderScene; all downstream rendering authority is scene-based.
    project_legacy_mesh_once(result);
    enforce_render_scene_contract(result);

    // Every accepted module can be inspected immediately. Format-specific
    // adapters replace this minimal root with a typed tree as they migrate.
    ensure_minimal_inspection(result);
    return result;
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
