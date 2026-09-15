#include "dmcresource/spider/session_actions.h"

#include <array>
#include <utility>

#include "dmcresource/composite_builder.h"
#include "dmcresource/spider/crusader.h"

namespace dmcresource::spider::actions {
namespace {

namespace crusader = dmcresource::spider::crusader;
constexpr crusader::OperationId kComposeMods = 1U;

struct ComposeState final {
    const std::vector<const Session*>* parts{};
    const std::vector<std::string>* names{};
    std::unique_ptr<Session> result;
};

bool compose_operation(void* raw, std::uint32_t) noexcept {
    auto* state = static_cast<ComposeState*>(raw);
    if (state == nullptr || state->parts == nullptr || state->names == nullptr) {
        return false;
    }
    try {
        auto built = composite_builder::build_mod_composite(
            *state->parts, *state->names,
            composite_builder::BuildOptions{
                .primary_host_index = 0U,
                .resolve_default_joint_attachments = true,
            });
        if (!built) return false;
        state->result = std::move(built.session);
        if (!state->result->trace.empty()) state->result->trace += "\n";
        state->result->trace += "[OK] spider.crusader.action.compose-mods";
        return true;
    } catch (...) {
        state->result.reset();
        return false;
    }
}

const crusader::Plan& compose_plan() {
    static const crusader::Plan plan = [] {
        crusader::Plan out;
        out.instructions.push_back({
            .operation = kComposeMods,
            .operand = 0U,
            .dependency_begin = 0U,
            .dependency_count = 0U,
            .domain = crusader::Domain::cpu,
        });
        return out;
    }();
    return plan;
}

}  // namespace

std::unique_ptr<Session> compose_mod_sessions(
    const std::vector<const Session*>& parts,
    const std::vector<std::string>& names) noexcept {
    ComposeState state{.parts = &parts, .names = &names};
    static const std::array bindings{
        crusader::OperationBinding{
            .operation = kComposeMods,
            .execute = &compose_operation,
        },
    };
    const auto report = crusader::execute(compose_plan(), bindings, &state);
    if (!report.ok()) return nullptr;
    return std::move(state.result);
}

}  // namespace dmcresource::spider::actions
