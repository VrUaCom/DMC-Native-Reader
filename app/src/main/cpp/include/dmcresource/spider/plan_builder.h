#pragma once

#include "dmcresource/spider/crusader.h"

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace dmcresource::spider::crusader {

// Same Builder as DMC Rengine spider/plan_builder.hpp (branch
// experiment/spider-python-migration), over the Reader-owned Crusader types.

// A node of a plan under construction (instruction index once built).
struct Node final {
    std::uint32_t index{std::numeric_limits<std::uint32_t>::max()};
};

// Builds a Plan from named dependencies instead of hand-maintained
// dependency_begin / dependency_count offsets. The executor's rules hold by
// construction: a node can only depend on nodes added before it, so the plan
// is topologically ordered; a dependency on an unknown node marks the builder
// failed and build() yields a plan the executor rejects (fail-closed), never a
// silently different graph.
//
// Labels are optional, cost nothing at execution time and turn an
// ExecutionReport into a readable step name ("transform[3]").
class Builder final {
public:
    Builder() = default;

    // Appends an instruction that runs after `after`; returns its node.
    Node add(OperationId operation,
                 std::uint32_t operand = 0U,
                 Domain domain = Domain::cpu,
                 std::initializer_list<Node> after = {},
                 std::string_view label = {}) {
        return add_span(operation, operand, domain,
                        std::span<const Node>{after.begin(), after.size()}, label);
    }

    // Same, with a runtime list of predecessors (fan-in after a fan-out).
    Node add_span(OperationId operation,
                      std::uint32_t operand,
                      Domain domain,
                      std::span<const Node> after,
                      std::string_view label = {}) {
        const auto index = plan_.instructions.size();
        if (index >= std::numeric_limits<std::uint32_t>::max() ||
            after.size() > std::numeric_limits<std::uint16_t>::max() ||
            plan_.dependencies.size() > std::numeric_limits<std::uint32_t>::max() - after.size()) {
            ok_ = false;
            return {};
        }
        Instruction instruction{};
        instruction.operation = operation;
        instruction.operand = operand;
        instruction.domain = domain;
        instruction.dependency_begin = static_cast<std::uint32_t>(plan_.dependencies.size());
        instruction.dependency_count = static_cast<std::uint16_t>(after.size());
        for (const auto node : after) {
            if (node.index >= index) ok_ = false;  // unknown or not earlier
            plan_.dependencies.push_back(node.index);
        }
        plan_.instructions.push_back(instruction);
        labels_.emplace_back(label);
        return {static_cast<std::uint32_t>(index)};
    }

    [[nodiscard]] bool ok() const noexcept { return ok_; }
    [[nodiscard]] std::size_t size() const noexcept { return plan_.instructions.size(); }

    // The plan; when the builder failed, an always-invalid plan so execution
    // stops with invalid_plan before any operation runs.
    [[nodiscard]] Plan build() const {
        if (ok_) return plan_;
        Plan invalid;
        invalid.instructions.push_back(Instruction{.dependency_begin = 1U, .dependency_count = 1U});
        return invalid;
    }

    [[nodiscard]] std::string_view label(std::size_t index) const noexcept {
        return index < labels_.size() ? std::string_view{labels_[index]} : std::string_view{};
    }

    // Label of the instruction a report stopped at, or empty.
    [[nodiscard]] std::string_view failed_label(const ExecutionReport& report) const noexcept {
        return report.failed_instruction == ExecutionReport::npos ? std::string_view{}
                                                                         : label(report.failed_instruction);
    }

private:
    Plan plan_;
    std::vector<std::string> labels_;
    bool ok_{true};
};

}  // namespace dmcresource::spider::crusader
