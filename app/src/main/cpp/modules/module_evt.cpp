#include "dmcresource/native_module.h"

#include "dmc_rengine/formats/evt.hpp"
#include "dmcresource/module_support.h"
#include "dmcresource/spider/crusader.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <span>
#include <sstream>
#include <string>
#include <utility>

namespace dmcresource {
namespace {

namespace evt = dmc::rengine::formats::evt;
namespace crusader = dmcresource::spider::crusader;

constexpr crusader::OperationId kProjectEventTbl = 1U;

[[nodiscard]] std::string hex_value(std::uint64_t value, int width) {
    std::ostringstream out;
    out << "0x" << std::uppercase << std::hex << std::setfill('0')
        << std::setw(width) << value;
    return out.str();
}

[[nodiscard]] EvidenceLevel opcode_evidence_level(evt::OpcodeEvidence evidence) noexcept {
    switch (evidence) {
    case evt::OpcodeEvidence::exe_confirmed:
        return EvidenceLevel::ExeConfirmed;
    case evt::OpcodeEvidence::corpus_structural_confirmed:
        return EvidenceLevel::StructuralConfirmed;
    case evt::OpcodeEvidence::corpus_semantic_candidate:
        return EvidenceLevel::Recognized;
    case evt::OpcodeEvidence::unknown:
        return EvidenceLevel::PreservedUndecoded;
    }
    return EvidenceLevel::PreservedUndecoded;
}

// Allocating projection helper. The OperationFn boundary below owns catch-all
// conversion to a no-allocation rejected PipelineResult.
PipelineResult project_evt(
    const NativeModule& module,
    const std::uint8_t* bytes,
    std::size_t size,
    const ProbeResult& probe) {
    if (bytes == nullptr) {
        return module_support::reject(
            probe, module.id, "EVT rejected: null input");
    }

    evt::ParseResult parsed;
    try {
        parsed = evt::Parser::parse(
            std::as_bytes(std::span<const std::uint8_t>{bytes, size}));
    } catch (...) {
        return module_support::reject(
            probe, module.id, "EVT rejected: parser allocation failed");
    }

    if (!parsed.ok()) {
        std::string detail = "EVT rejected by canonical structural parser";
        for (const auto& diagnostic : parsed.diagnostics) {
            if (diagnostic.severity == dmc::rengine::formats::ParseSeverity::error) {
                detail += ": ";
                detail += diagnostic.message;
                break;
            }
        }
        return module_support::reject(probe, module.id, std::move(detail));
    }

    std::ostringstream detail;
    detail << "EventTbl | streams=" << parsed.document.header.stream_count
           << " commands=" << parsed.document.commands.size()
           << " terminal="
           << hex_value(parsed.document.header.terminal_command_offset, 8);
    auto out = structural_pipeline(probe, module.id, detail.str());
    out.capabilities = capability(ResourceCapability::Inspection);
    out.inspection.format = "EventTbl";
    out.inspection.root.id = "evt";
    out.inspection.root.title = "Event Table";
    out.inspection.root.kind = InspectionKind::Document;
    out.inspection.root.source_span = SourceSpan{0U, size};
    out.inspection.root.properties.push_back({
        "Magic", "EVT\\0", EvidenceLevel::StructuralConfirmed});
    out.inspection.root.properties.push_back({
        "PackedHeader", hex_value(parsed.document.header.version, 8),
        EvidenceLevel::StructuralConfirmed});
    out.inspection.root.properties.push_back({
        "Revision", std::to_string(parsed.document.header.revision),
        EvidenceLevel::StructuralConfirmed});
    out.inspection.root.properties.push_back({
        "StreamCount", std::to_string(parsed.document.header.stream_count),
        EvidenceLevel::StructuralConfirmed});
    out.inspection.root.properties.push_back({
        "TerminalCommandOffset",
        hex_value(parsed.document.header.terminal_command_offset, 8),
        EvidenceLevel::StructuralConfirmed});
    out.inspection.root.properties.push_back({
        "CommandCount", std::to_string(parsed.document.commands.size()),
        EvidenceLevel::StructuralConfirmed});

    InspectionNode streams;
    streams.id = "streams";
    streams.title = "Streams";
    streams.kind = InspectionKind::Collection;

    InspectionNode commands;
    commands.id = "commands";
    commands.title = "Commands";
    commands.kind = InspectionKind::Collection;

    try {
        streams.children.reserve(parsed.document.stream_offsets.size());
        for (std::size_t index = 0U;
             index < parsed.document.stream_offsets.size();
             ++index) {
            InspectionNode node;
            node.id = "stream-" + std::to_string(index);
            node.title = "Stream " + std::to_string(index);
            node.kind = InspectionKind::Object;
            node.properties.push_back({
                "Offset", hex_value(parsed.document.stream_offsets[index], 8),
                EvidenceLevel::StructuralConfirmed});
            streams.children.push_back(std::move(node));
        }

        commands.children.reserve(parsed.document.commands.size());
        for (std::size_t index = 0U;
             index < parsed.document.commands.size();
             ++index) {
            const auto& command = parsed.document.commands[index];
            const auto descriptor = command.descriptor();
            const auto semantic_evidence = opcode_evidence_level(descriptor.evidence);

            InspectionNode node;
            node.id = "command-" + std::to_string(index);
            node.title = descriptor.evidence == evt::OpcodeEvidence::unknown
                ? "Command " + std::to_string(index)
                : std::string{descriptor.name};
            node.kind = InspectionKind::Object;
            node.source_span = SourceSpan{
                command.offset, command.serialized_size()};
            node.properties.push_back({
                "Offset", hex_value(command.offset, 8),
                EvidenceLevel::StructuralConfirmed});
            node.properties.push_back({
                "Opcode", hex_value(command.opcode, 2),
                EvidenceLevel::StructuralConfirmed});
            node.properties.push_back({
                "ArgumentCount", std::to_string(command.argument_count),
                EvidenceLevel::StructuralConfirmed});
            node.properties.push_back({
                "SemanticName", std::string{descriptor.name}, semantic_evidence});
            node.properties.push_back({
                "SemanticClass",
                std::string{evt::to_string(descriptor.semantic_class)},
                semantic_evidence});
            node.properties.push_back({
                "SemanticEvidence",
                std::string{evt::to_string(descriptor.evidence)},
                semantic_evidence});
            for (std::size_t argument = 0U;
                 argument < command.arguments.size();
                 ++argument) {
                node.properties.push_back({
                    "Arg" + std::to_string(argument),
                    hex_value(command.arguments[argument], 8),
                    EvidenceLevel::PreservedUndecoded});
            }
            commands.children.push_back(std::move(node));
        }

        for (std::size_t index = 0U; index < parsed.diagnostics.size(); ++index) {
            const auto& diagnostic = parsed.diagnostics[index];
            InspectionNode node;
            node.id = "diagnostic-" + std::to_string(index);
            node.title = diagnostic.code;
            node.kind = InspectionKind::Diagnostic;
            node.source_span = SourceSpan{diagnostic.offset, 0U};
            node.properties.push_back({
                "Message", diagnostic.message,
                diagnostic.severity == dmc::rengine::formats::ParseSeverity::warning
                    ? EvidenceLevel::PreservedUndecoded
                    : EvidenceLevel::StructuralConfirmed});
            out.inspection.root.children.push_back(std::move(node));
        }
        out.inspection.root.children.push_back(std::move(streams));
        out.inspection.root.children.push_back(std::move(commands));
    } catch (...) {
        return module_support::reject(
            probe, module.id, "EVT rejected: inspection allocation failed");
    }

    return out;
}

struct EventExecutionState final {
    const NativeModule* module{};
    const std::uint8_t* bytes{};
    std::size_t size{};
    const ProbeResult* probe{};
    PipelineResult result{};
};

bool project_event_operation(void* raw, std::uint32_t) noexcept {
    auto* state = static_cast<EventExecutionState*>(raw);
    if (state == nullptr || state->module == nullptr || state->probe == nullptr) {
        return false;
    }
    try {
        state->result = project_evt(
            *state->module, state->bytes, state->size, *state->probe);
        return state->result.accepted;
    } catch (...) {
        state->result = module_support::reject_minimal(*state->probe);
        return false;
    }
}

const crusader::Plan& event_plan() {
    static const crusader::Plan plan = [] {
        crusader::Plan out;
        out.instructions.push_back({
            .operation = kProjectEventTbl,
            .operand = 0U,
            .dependency_begin = 0U,
            .dependency_count = 0U,
            .domain = crusader::Domain::cpu,
        });
        return out;
    }();
    return plan;
}

PipelineResult run_evt_module(
    const NativeModule& module,
    std::string_view,
    const std::uint8_t* bytes,
    std::size_t size,
    const ProbeResult& probe) noexcept {
    try {
        EventExecutionState state{
            .module = &module,
            .bytes = bytes,
            .size = size,
            .probe = &probe,
        };
        static const std::array bindings{
            crusader::OperationBinding{
                .operation = kProjectEventTbl,
                .execute = &project_event_operation,
            },
        };

        // event_plan() may allocate its NativePlan on first use; keep that
        // initialization inside this ModuleRun exception boundary.
        const auto report = crusader::execute(event_plan(), bindings, &state);
        if (!report.ok()) {
            if (!state.result.detail.empty()) return state.result;
            std::string detail = "Crusader EventTbl execution failed: ";
            detail += crusader::to_string(report.status);
            return module_support::reject(probe, module.id, std::move(detail));
        }
        state.result.modules.push_back({"spider.crusader", true});
        return state.result;
    } catch (...) {
        return module_support::reject_minimal(probe);
    }
}

} // namespace

NativeModule evt_module() noexcept {
    return {
        "formats.evt.structural-reader",
        "EventTbl",
        Format::Evt,
        ModuleKind::Structural,
        false,
        run_evt_module,
        capability(ResourceCapability::Inspection),
    };
}

} // namespace dmcresource
