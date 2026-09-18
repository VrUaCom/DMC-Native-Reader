#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

#include "dmcresource/decode_pipeline.h"
#include "dmcresource/spider/crusader.h"

namespace {

void put_u32(std::vector<std::uint8_t>& bytes,
             std::size_t offset,
             std::uint32_t value) {
    assert(offset + 4U <= bytes.size());
    for (std::size_t index = 0U; index < 4U; ++index) {
        bytes[offset + index] = static_cast<std::uint8_t>(
            (value >> (index * 8U)) & 0xFFU);
    }
}

std::vector<std::uint8_t> make_event_table() {
    std::vector<std::uint8_t> bytes(0x40U, 0U);
    bytes[0] = 'E';
    bytes[1] = 'V';
    bytes[2] = 'T';
    bytes[3] = 0U;
    put_u32(bytes, 0x04U, 0x00010001U);
    put_u32(bytes, 0x20U, 0x00000102U);
    put_u32(bytes, 0x24U, 0x37U);
    put_u32(bytes, 0x28U, 0x00000001U);
    put_u32(bytes, 0x2CU, 0x00000020U);
    put_u32(bytes, 0x08U, 0x2CU);
    return bytes;
}

struct CrusaderState final {
    std::array<std::uint32_t, 8U> order{};
    std::size_t count{};
    std::uint32_t fail_operand{0xFFFFFFFFU};
};

bool record_crusader(void* raw, std::uint32_t operand) noexcept {
    auto* state = static_cast<CrusaderState*>(raw);
    if (state == nullptr || state->count >= state->order.size()) return false;
    if (operand == state->fail_operand) return false;
    state->order[state->count++] = operand;
    return true;
}

bool record_crusader_offset(void* raw, std::uint32_t operand) noexcept {
    auto* state = static_cast<CrusaderState*>(raw);
    if (state == nullptr || state->count >= state->order.size()) return false;
    state->order[state->count++] = operand + 100U;
    return true;
}

void verify_crusader_kernel() {
    namespace crusader = dmcresource::spider::crusader;

    constexpr crusader::OperationId kProbe = 1U;
    constexpr crusader::OperationId kParse = 2U;
    constexpr crusader::OperationId kProject = 3U;

    const std::array bindings{
        crusader::OperationBinding{.operation = kProbe, .execute = &record_crusader},
        crusader::OperationBinding{.operation = kParse, .execute = &record_crusader},
        crusader::OperationBinding{
            .operation = kProject, .execute = &record_crusader_offset},
    };

    crusader::Plan plan;
    plan.dependencies = {0U, 1U};
    plan.instructions = {
        crusader::Instruction{
            .operation = kProbe,
            .operand = 10U,
            .dependency_begin = 0U,
            .dependency_count = 0U,
            .domain = crusader::Domain::cpu,
        },
        crusader::Instruction{
            .operation = kParse,
            .operand = 20U,
            .dependency_begin = 0U,
            .dependency_count = 1U,
            .domain = crusader::Domain::cpu,
        },
        crusader::Instruction{
            .operation = kProject,
            .operand = 30U,
            .dependency_begin = 1U,
            .dependency_count = 1U,
            .domain = crusader::Domain::cpu,
        },
    };

    assert(plan.structurally_valid());
    CrusaderState state;
    const auto ok = crusader::execute(plan, bindings, &state);
    assert(ok.ok());
    assert(ok.completed_instructions == 3U);
    assert(state.count == 3U);
    assert(state.order[0U] == 10U);
    assert(state.order[1U] == 20U);
    assert(state.order[2U] == 130U);

    const std::array partial_bindings{bindings[0U], bindings[1U]};
    state = {};
    const auto missing = crusader::execute(plan, partial_bindings, &state);
    assert(missing.status == crusader::ExecutionStatus::missing_operation);
    assert(missing.completed_instructions == 2U);
    assert(missing.failed_instruction == 2U);

    state = {};
    state.fail_operand = 20U;
    const auto failed = crusader::execute(plan, bindings, &state);
    assert(failed.status == crusader::ExecutionStatus::operation_failed);
    assert(failed.completed_instructions == 1U);
    assert(failed.failed_instruction == 1U);
    assert(state.count == 1U);

    auto invalid = plan;
    invalid.dependencies[0U] = 1U;
    assert(!invalid.structurally_valid());
    const auto invalid_report = crusader::execute(invalid, bindings, &state);
    assert(invalid_report.status == crusader::ExecutionStatus::invalid_plan);

    const std::array duplicate_bindings{bindings[0U], bindings[0U]};
    const auto duplicate = crusader::execute(plan, duplicate_bindings, &state);
    assert(duplicate.status == crusader::ExecutionStatus::invalid_bindings);

    const std::array null_bindings{
        crusader::OperationBinding{.operation = kProbe, .execute = nullptr}};
    const auto null_binding = crusader::execute(plan, null_bindings, &state);
    assert(null_binding.status == crusader::ExecutionStatus::invalid_bindings);

    crusader::Plan empty;
    assert(empty.structurally_valid());
    const auto empty_report = crusader::execute(empty, bindings, &state);
    assert(empty_report.ok());
    assert(empty_report.completed_instructions == 0U);
}

bool trace_contains(const dmcresource::PipelineResult& result,
                    std::string_view id) {
    for (const auto& module : result.modules) {
        if (module.name != nullptr && std::string_view{module.name} == id &&
            module.complete) {
            return true;
        }
    }
    return false;
}

}  // namespace

int main() {
    verify_crusader_kernel();

    const auto bytes = make_event_table();
    const auto result = dmcresource::run_decode_pipeline(
        "EventTbl00.bin", bytes.data(), bytes.size());

    assert(result.accepted);
    assert(!result.renderable);
    assert(result.inspection.format == "EventTbl");
    assert(trace_contains(result, "formats.evt.structural-reader"));
    assert(trace_contains(result, "spider.crusader"));
    return 0;
}
