#include "dmcresource/spider/model_placement_actions.h"

#include <array>

#include "dmcresource/resource_session.h"
#include "dmcresource/spider/crusader.h"

namespace dmcresource::spider::actions {
namespace {

namespace crusader = dmcresource::spider::crusader;
constexpr crusader::OperationId kAttachModelPart = 3U;
constexpr crusader::OperationId kResetModelPart = 4U;

struct PlacementState final {
    Session* session{};
    std::size_t host_part_index{kNoCompositePart};
    std::size_t child_part_index{kNoCompositePart};
    std::uint32_t joint_index{kNoAttachmentSelector};
    composite_placement::PlacementResult result{};
};

void append_trace(Session* session, const char* text) noexcept {
    if (session == nullptr || text == nullptr) return;
    try {
        if (!session->trace.empty()) session->trace += "\n";
        session->trace += text;
    } catch (...) {
        // Trace is diagnostic only and must never roll back an applied placement.
    }
}

bool attach_operation(void* raw, std::uint32_t) noexcept {
    auto* state = static_cast<PlacementState*>(raw);
    if (state == nullptr || state->session == nullptr) return false;
    state->result = composite_placement::attach_to_host_joint(
        state->session,
        state->host_part_index,
        state->child_part_index,
        state->joint_index);
    if (!state->result.ok()) return false;
    append_trace(
        state->session,
        "[OK] spider.crusader.action.attach-mod-part-to-host-joint");
    return true;
}

bool reset_operation(void* raw, std::uint32_t) noexcept {
    auto* state = static_cast<PlacementState*>(raw);
    if (state == nullptr || state->session == nullptr) return false;
    state->result = composite_placement::reset_to_source_coordinates(
        state->session,
        state->child_part_index);
    if (!state->result.ok()) return false;
    append_trace(
        state->session,
        "[OK] spider.crusader.action.reset-mod-part-placement");
    return true;
}

[[nodiscard]] const crusader::Plan& one_step_plan(crusader::OperationId operation) {
    static const crusader::Plan attach = [] {
        crusader::Plan out;
        out.instructions.push_back({
            .operation = kAttachModelPart,
            .operand = 0U,
            .dependency_begin = 0U,
            .dependency_count = 0U,
            .domain = crusader::Domain::cpu,
        });
        return out;
    }();
    static const crusader::Plan reset = [] {
        crusader::Plan out;
        out.instructions.push_back({
            .operation = kResetModelPart,
            .operand = 0U,
            .dependency_begin = 0U,
            .dependency_count = 0U,
            .domain = crusader::Domain::cpu,
        });
        return out;
    }();
    return operation == kAttachModelPart ? attach : reset;
}

}  // namespace

composite_placement::PlacementResult attach_mod_part_to_host_joint(
    Session* session,
    std::size_t host_part_index,
    std::size_t child_part_index,
    std::uint32_t host_joint_index) noexcept {
    PlacementState state{
        .session = session,
        .host_part_index = host_part_index,
        .child_part_index = child_part_index,
        .joint_index = host_joint_index,
    };
    static const std::array bindings{
        crusader::OperationBinding{
            .operation = kAttachModelPart,
            .execute = &attach_operation,
        },
    };
    const auto report = crusader::execute(one_step_plan(kAttachModelPart), bindings, &state);
    if (!report.ok() && state.result.ok()) {
        state.result.status = composite_placement::PlacementStatus::CompositeProjectionMismatch;
    }
    return state.result;
}

composite_placement::PlacementResult reset_mod_part_placement(
    Session* session,
    std::size_t child_part_index) noexcept {
    PlacementState state{
        .session = session,
        .child_part_index = child_part_index,
    };
    static const std::array bindings{
        crusader::OperationBinding{
            .operation = kResetModelPart,
            .execute = &reset_operation,
        },
    };
    const auto report = crusader::execute(one_step_plan(kResetModelPart), bindings, &state);
    if (!report.ok() && state.result.ok()) {
        state.result.status = composite_placement::PlacementStatus::CompositeProjectionMismatch;
    }
    return state.result;
}

}  // namespace dmcresource::spider::actions
