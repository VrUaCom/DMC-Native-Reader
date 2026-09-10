#pragma once

#include "dmc_rengine/spider/native_executor.hpp"

namespace dmcresource::spider::crusader {

// Product-facing Native Reader name for compact C++ module orchestration.
// This is deliberately a zero-overhead facade over the pinned ReaderCore
// generic native executor. No executor, plan, dependency graph, binding, or
// execution-domain implementation is duplicated in Native Reader.
using OperationId = dmc::rengine::spider::NativeOperationId;
using OperationFn = dmc::rengine::spider::NativeOperationFn;
using Instruction = dmc::rengine::spider::NativeInstruction;
using Plan = dmc::rengine::spider::NativePlan;
using OperationBinding = dmc::rengine::spider::NativeOperationBinding;
using ExecutionStatus = dmc::rengine::spider::NativeExecutionStatus;
using ExecutionReport = dmc::rengine::spider::NativeExecutionReport;
using Domain = dmc::rengine::spider::ExecutionDomain;

[[nodiscard]] inline ExecutionReport execute(
    const Plan& plan,
    std::span<const OperationBinding> bindings,
    void* state) noexcept {
    return dmc::rengine::spider::execute_native_plan(plan, bindings, state);
}

[[nodiscard]] constexpr const char* to_string(
    ExecutionStatus status) noexcept {
    return dmc::rengine::spider::to_string(status);
}

}  // namespace dmcresource::spider::crusader
