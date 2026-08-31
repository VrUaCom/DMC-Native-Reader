#include "dmcresource/decode_pipeline.h"

#include <sstream>
#include <string_view>
#include <utility>

#include "dmcresource/decode.h"

namespace dmcresource {
namespace {

void add_common_modules(PipelineResult& out) {
    out.modules.push_back({"identity-probe", true});
    out.modules.push_back({"bounded-read-guard", true});
}

bool family_is(const ProbeResult& probe_result, std::string_view family) {
    return probe_result.family != nullptr && std::string_view{probe_result.family} == family;
}

void add_partial_model_adapter(PipelineResult& out,
                               const char* adapter,
                               const char* pending_a,
                               const char* pending_b) {
    out.modules.push_back({"shared-model-family-envelope", true});
    out.modules.push_back({adapter, true});
    out.modules.push_back({pending_a, false});
    out.modules.push_back({pending_b, false});
}

}  // namespace

PipelineResult run_decode_pipeline(std::string_view filename,
                                   const std::uint8_t* bytes,
                                   std::size_t size) noexcept {
    PipelineResult out;
    out.probe = probe(filename, bytes, size);
    if (!out.probe.recognized) {
        out.detail = "pipeline rejected unknown resource";
        return out;
    }

    out.accepted = true;
    add_common_modules(out);

    if (family_is(out.probe, "MOD") || family_is(out.probe, "SCM")) {
        out.modules.push_back({"shared-model-header", true});
        out.modules.push_back({"object-table", true});
        out.modules.push_back({"mesh-table", true});
        out.modules.push_back({"vertex-stream", true});
        out.modules.push_back({"topology", true});
        out.modules.push_back({family_is(out.probe, "SCM")
                                   ? "scm-scene-transform-adapter"
                                   : "mod-model-adapter",
                               true});

        auto decoded = decode_resource(filename, bytes, size);
        if (decoded.status != DecodeStatus::Ok) {
            out.accepted = false;
            out.detail = decoded.detail != nullptr ? decoded.detail : "mesh module rejected resource";
            return out;
        }
        out.mesh = std::move(decoded.mesh);
        out.renderable = true;
        out.detail = decoded.detail != nullptr ? decoded.detail : "shared geometry pipeline complete";
        return out;
    }

    if (family_is(out.probe, "EFM")) {
        add_partial_model_adapter(out,
                                  "efm-family-adapter",
                                  "efm-vertex-stream-binding",
                                  "efm-material-topology-binding");
        out.detail = describe_resource(filename, bytes, size, out.probe) +
                     "\nEFM adapter reuses the shared model envelope; geometry remains disabled until the pending stream bindings are evidence-closed.";
        return out;
    }

    if (family_is(out.probe, "MRP")) {
        add_partial_model_adapter(out,
                                  "mrp-render-family-adapter",
                                  "mrp-record-schema",
                                  "mrp-downstream-owner-binding");
        out.detail = describe_resource(filename, bytes, size, out.probe) +
                     "\nMRP adapter is intentionally non-renderable until exact record ownership/schema is recovered.";
        return out;
    }

    if (family_is(out.probe, "SHW")) {
        add_partial_model_adapter(out,
                                  "shw-shadow-family-adapter",
                                  "shw-triangle-index-module",
                                  "shw-external-spatial-pool-binding");
        out.detail = describe_resource(filename, bytes, size, out.probe) +
                     "\nSHW adapter remains non-renderable until topology and the external spatial/vector pool are bound safely.";
        return out;
    }

    out.modules.push_back({"structural-inspection-adapter", true});
    out.detail = describe_resource(filename, bytes, size, out.probe);
    return out;
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
