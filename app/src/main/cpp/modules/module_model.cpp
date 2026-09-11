#include "dmcresource/native_module.h"

#include <array>
#include <string>
#include <string_view>
#include <utility>

#include "dmcresource/adapters/mod_adapter.h"
#include "dmcresource/adapters/scm_adapter.h"
#include "dmcresource/module_support.h"
#include "dmcresource/spider/crusader.h"

namespace dmcresource {
namespace {

namespace crusader = dmcresource::spider::crusader;

constexpr crusader::OperationId kProjectModel = 1U;

struct ModelExecutionState final {
    const NativeModule* module{};
    const std::uint8_t* bytes{};
    std::size_t size{};
    const ProbeResult* probe{};
    PipelineResult result{};
};

// Spider owns execution/orchestration; the canonical adapters continue to own
// MOD/SCM format projection. Keeping this switch inside the native operation
// avoids duplicating the executor while preserving one bounded adapter per format.
bool project_model_operation(void* raw, std::uint32_t) noexcept {
    auto* state = static_cast<ModelExecutionState*>(raw);
    if (state == nullptr || state->module == nullptr || state->probe == nullptr) {
        return false;
    }

    switch (state->module->format) {
    case Format::Scm:
        state->result = adapters::run_scm_adapter(
            *state->probe, state->bytes, state->size, state->module->id);
        break;
    case Format::Mod:
        state->result = adapters::run_mod_adapter(
            *state->probe, state->bytes, state->size, state->module->id);
        break;
    default:
        state->result = module_support::reject(
            *state->probe, state->module->id,
            "Model pipeline rejected: invalid module route");
        return false;
    }
    return state->result.accepted;
}

const crusader::Plan& model_plan() {
    static const crusader::Plan plan = [] {
        crusader::Plan out;
        out.instructions.push_back(crusader::Instruction{
            .operation = kProjectModel,
            .operand = 0U,
            .dependency_begin = 0U,
            .dependency_count = 0U,
            .domain = crusader::Domain::cpu,
        });
        return out;
    }();
    return plan;
}

PipelineResult run_model_module(const NativeModule& module,
                                std::string_view,
                                const std::uint8_t* bytes,
                                std::size_t size,
                                const ProbeResult& probe) noexcept {
    ModelExecutionState state{
        .module = &module,
        .bytes = bytes,
        .size = size,
        .probe = &probe,
    };

    static const std::array bindings{
        crusader::OperationBinding{
            .operation = kProjectModel,
            .execute = &project_model_operation,
        },
    };

    const auto report = crusader::execute(model_plan(), bindings, &state);
    if (!report.ok()) {
        if (!state.result.detail.empty()) return state.result;
        std::string detail = "Crusader model execution failed: ";
        detail += crusader::to_string(report.status);
        return module_support::reject(probe, module.id, std::move(detail));
    }

    state.result.modules.push_back({"spider.crusader", true});
    return state.result;
}

}  // namespace

NativeModule scm_module() noexcept {
    const auto caps = capability(ResourceCapability::Inspection) |
        ResourceCapability::Geometry |
        ResourceCapability::Wireframe |
        ResourceCapability::NodeHierarchy |
        ResourceCapability::TextureBinding |
        ResourceCapability::UvCoordinates;
    return {"formats.scm.mesh-reader", "SCM", Format::Scm,
            ModuleKind::Mesh, true, run_model_module, caps};
}

NativeModule mod_module() noexcept {
    const auto caps = capability(ResourceCapability::Inspection) |
        ResourceCapability::Geometry |
        ResourceCapability::Wireframe |
        ResourceCapability::NodeHierarchy |
        ResourceCapability::SkeletalSkinning |
        ResourceCapability::SkinWeights |
        ResourceCapability::TextureBinding |
        ResourceCapability::UvCoordinates;
    return {"formats.mod.mesh-reader", "MOD", Format::Mod,
            ModuleKind::Mesh, true, run_model_module, caps};
}

}  // namespace dmcresource
