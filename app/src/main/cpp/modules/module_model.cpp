#include "dmcresource/native_module.h"

#include <array>
#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <utility>

#include "dmc_rengine/formats/mod.hpp"
#include "dmcresource/adapters/mod_adapter.h"
#include "dmcresource/adapters/scm_adapter.h"
#include "dmcresource/module_support.h"
#include "dmcresource/spider/crusader.h"

namespace dmcresource {
namespace {

namespace crusader = dmcresource::spider::crusader;
namespace canonical_mod = dmc::rengine::formats::mod;

constexpr crusader::OperationId kProjectModel = 1U;

struct ModelExecutionState final {
    const NativeModule* module{};
    const std::uint8_t* bytes{};
    std::size_t size{};
    const ProbeResult* probe{};
    PipelineResult result{};
};

void publish_mod_attachment_selector(ModelExecutionState* state) noexcept {
    if (state == nullptr || !state->result.accepted ||
        state->module == nullptr || state->module->format != Format::Mod ||
        (state->size != 0U && state->bytes == nullptr)) {
        return;
    }

    // Keep raw MOD layout knowledge in canonical Rengine. The MOD adapter owns
    // the full format projection; this small post-projection bridge publishes
    // only the already-proven +0x13 semantic into the platform-neutral scene.
    // It can be folded into the adapter return object later without changing
    // the public composition contract.
    try {
        const auto bytes = std::span<const std::byte>{
            reinterpret_cast<const std::byte*>(state->bytes), state->size};
        const auto parsed = canonical_mod::Parser::parse(bytes);
        if (!parsed.ok()) return;
        state->result.scene.default_attachment_selector =
            static_cast<std::uint32_t>(parsed.document.header.default_joint_index());
    } catch (...) {
        state->result.scene.default_attachment_selector.reset();
    }
}

// Spider owns execution/orchestration; the canonical adapters continue to own
// MOD/SCM format projection. Keeping this switch inside the native operation
// avoids duplicating the executor while preserving one bounded adapter per format.
bool project_model_operation(void* raw, std::uint32_t) noexcept {
    auto* state = static_cast<ModelExecutionState*>(raw);
    if (state == nullptr || state->module == nullptr || state->probe == nullptr) {
        return false;
    }

    try {
        switch (state->module->format) {
        case Format::Scm:
            state->result = adapters::run_scm_adapter(
                *state->probe, state->bytes, state->size, state->module->id);
            break;
        case Format::Mod:
            state->result = adapters::run_mod_adapter(
                *state->probe, state->bytes, state->size, state->module->id);
            publish_mod_attachment_selector(state);
            break;
        default:
            state->result = module_support::reject(
                *state->probe, state->module->id,
                "Model pipeline rejected: invalid module route");
            return false;
        }
        return state->result.accepted;
    } catch (...) {
        state->result = module_support::reject_minimal(*state->probe);
        return false;
    }
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
    try {
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

        // model_plan() may allocate on first use. Keep that initialization inside
        // the ModuleRun exception boundary so allocation failure is a rejected
        // resource rather than std::terminate from this noexcept ABI.
        const auto report = crusader::execute(model_plan(), bindings, &state);
        if (!report.ok()) {
            if (!state.result.detail.empty()) return state.result;
            std::string detail = "Crusader model execution failed: ";
            detail += crusader::to_string(report.status);
            return module_support::reject(probe, module.id, std::move(detail));
        }

        state.result.modules.push_back({"spider.crusader", true});
        return state.result;
    } catch (...) {
        return module_support::reject_minimal(probe);
    }
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
