#include "dmcresource/spider/crusader.h"

#include <algorithm>

namespace dmcresource::spider::crusader {
namespace {

[[nodiscard]] const OperationBinding* find_binding(
    std::span<const OperationBinding> bindings,
    OperationId operation) noexcept {
    const auto it = std::find_if(
        bindings.begin(), bindings.end(),
        [operation](const OperationBinding& binding) {
            return binding.operation == operation;
        });
    return it == bindings.end() ? nullptr : &*it;
}

[[nodiscard]] bool bindings_valid(
    std::span<const OperationBinding> bindings) noexcept {
    for (std::size_t index = 0U; index < bindings.size(); ++index) {
        if (bindings[index].execute == nullptr) {
            return false;
        }
        for (std::size_t other = index + 1U; other < bindings.size(); ++other) {
            if (bindings[index].operation == bindings[other].operation) {
                return false;
            }
        }
    }
    return true;
}

}  // namespace

bool Plan::structurally_valid() const noexcept {
    for (std::size_t index = 0U; index < instructions.size(); ++index) {
        const auto& instruction = instructions[index];
        const auto begin =
            static_cast<std::size_t>(instruction.dependency_begin);
        const auto count =
            static_cast<std::size_t>(instruction.dependency_count);
        if (begin > dependencies.size() ||
            count > dependencies.size() - begin) {
            return false;
        }
        for (std::size_t offset = 0U; offset < count; ++offset) {
            const auto dependency = static_cast<std::size_t>(
                dependencies[begin + offset]);
            if (dependency >= index) {
                return false;
            }
        }
    }
    return true;
}

ExecutionReport execute(
    const Plan& plan,
    std::span<const OperationBinding> bindings,
    void* state) noexcept {
    if (!plan.structurally_valid()) {
        return {
            .status = ExecutionStatus::invalid_plan,
            .completed_instructions = 0U,
            .failed_instruction = ExecutionReport::npos,
        };
    }
    if (!bindings_valid(bindings)) {
        return {
            .status = ExecutionStatus::invalid_bindings,
            .completed_instructions = 0U,
            .failed_instruction = ExecutionReport::npos,
        };
    }

    for (std::size_t index = 0U; index < plan.instructions.size(); ++index) {
        const auto& instruction = plan.instructions[index];
        const auto* binding = find_binding(bindings, instruction.operation);
        if (binding == nullptr) {
            return {
                .status = ExecutionStatus::missing_operation,
                .completed_instructions = index,
                .failed_instruction = index,
            };
        }
        if (!binding->execute(state, instruction.operand)) {
            return {
                .status = ExecutionStatus::operation_failed,
                .completed_instructions = index,
                .failed_instruction = index,
            };
        }
    }

    return {
        .status = ExecutionStatus::ok,
        .completed_instructions = plan.instructions.size(),
        .failed_instruction = ExecutionReport::npos,
    };
}

}  // namespace dmcresource::spider::crusader
