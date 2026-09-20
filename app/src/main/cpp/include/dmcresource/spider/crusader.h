#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <vector>

namespace dmcresource::spider::crusader {

// Reader-owned, format-neutral orchestration kernel.
//
// Canonical DMC parsing/format/runtime semantics remain in their owning
// ReaderCore/Rengine modules. Crusader owns only compact operation plans,
// dependency validation and deterministic dispatch into those product actions.
using OperationId = std::uint32_t;
using OperationFn = bool (*)(void* state, std::uint32_t operand) noexcept;

enum class Domain : std::uint8_t {
    cpu,
    io,
    gpu_graphics,
    gpu_compute,
    gpu_transfer,
};

struct Instruction final {
    OperationId operation{};
    std::uint32_t operand{};
    std::uint32_t dependency_begin{};
    std::uint16_t dependency_count{};
    Domain domain{Domain::cpu};
    std::uint8_t reserved{};
};

static_assert(sizeof(Instruction) == 16U,
              "Crusader instructions stay compact and ABI-simple");

struct Plan final {
    std::vector<Instruction> instructions;
    std::vector<std::uint32_t> dependencies;

    [[nodiscard]] bool structurally_valid() const noexcept;
};

struct OperationBinding final {
    OperationId operation{};
    OperationFn execute{};
};

enum class ExecutionStatus : std::uint8_t {
    ok,
    invalid_plan,
    invalid_bindings,
    missing_operation,
    operation_failed,
};

[[nodiscard]] constexpr const char* to_string(
    ExecutionStatus status) noexcept {
    switch (status) {
    case ExecutionStatus::ok: return "ok";
    case ExecutionStatus::invalid_plan: return "invalid-plan";
    case ExecutionStatus::invalid_bindings: return "invalid-bindings";
    case ExecutionStatus::missing_operation: return "missing-operation";
    case ExecutionStatus::operation_failed: return "operation-failed";
    }
    return "invalid-plan";
}

struct ExecutionReport final {
    static constexpr std::size_t npos = std::numeric_limits<std::size_t>::max();

    ExecutionStatus status{ExecutionStatus::invalid_plan};
    std::size_t completed_instructions{};
    std::size_t failed_instruction{npos};

    [[nodiscard]] bool ok() const noexcept {
        return status == ExecutionStatus::ok;
    }
};

// Executes a structurally validated, topologically ordered plan serially.
// Domain is preserved as scheduling metadata. This kernel deliberately owns no
// threads, GPU queues, parsers, file formats, UI policy or resource semantics.
[[nodiscard]] ExecutionReport execute(
    const Plan& plan,
    std::span<const OperationBinding> bindings,
    void* state) noexcept;

}  // namespace dmcresource::spider::crusader
