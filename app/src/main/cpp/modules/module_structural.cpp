#include "dmcresource/native_module.h"

#include <cstddef>
#include <cstdint>
#include <sstream>
#include <string>

#include "dmcresource/binary_reader.h"
#include "dmcresource/module_support.h"

namespace dmcresource {
namespace {

PipelineResult run_dca(std::string_view,
                       const std::uint8_t* bytes,
                       std::size_t size,
                       const ProbeResult& probe,
                       const char* id) noexcept {
    constexpr std::size_t header = 0x10U;
    constexpr std::size_t record = 0x410U;
    const BinaryReader reader(bytes, size);
    if (size < header || !module_support::magic4(reader, 0U, 'D', 'C', 'A', '\0') ||
        (size - header) % record != 0U) {
        return module_support::reject(
            probe, id,
            "DCA rejected: expected DCA\\0 + 0x10 header + integral 0x410 records");
    }
    std::ostringstream out;
    out << "DCA structural records=" << ((size - header) / record)
        << " stride=0x410";
    return structural_pipeline(probe, id, out.str());
}

PipelineResult run_lighting(std::size_t size,
                            const ProbeResult& probe,
                            const char* id) noexcept {
    constexpr std::size_t header = 0x20U;
    constexpr std::size_t record = 0x30U;
    if (size < header || (size - header) % record != 0U) {
        return module_support::reject(
            probe, id,
            "lighting resource rejected: expected 0x20 header + integral 0x30 records");
    }
    std::ostringstream out;
    out << probe.family << " structural records=" << ((size - header) / record)
        << " stride=0x30";
    return structural_pipeline(probe, id, out.str());
}

PipelineResult run_slot_container(std::string_view filename,
                                  const std::uint8_t* bytes,
                                  std::size_t size,
                                  const ProbeResult& probe,
                                  const char* id) noexcept {
    const BinaryReader reader(bytes, size);
    std::uint32_t slots = 0U;
    if (size < 8U || !reader.read_le(4U, &slots) ||
        slots > (size - 8U) / 4U) {
        return module_support::reject(
            probe, id, "relative-slot container header/table is invalid");
    }
    return structural_pipeline(probe, id,
                               describe_resource(filename, bytes, size, probe));
}

PipelineResult run_dca_module(const NativeModule& module,
                              std::string_view filename,
                              const std::uint8_t* bytes,
                              std::size_t size,
                              const ProbeResult& probe) noexcept {
    return run_dca(filename, bytes, size, probe, module.id);
}

PipelineResult run_lig_module(const NativeModule& module,
                              std::string_view,
                              const std::uint8_t*,
                              std::size_t size,
                              const ProbeResult& probe) noexcept {
    return run_lighting(size, probe, module.id);
}

PipelineResult run_lig2_module(const NativeModule& module,
                               std::string_view,
                               const std::uint8_t*,
                               std::size_t size,
                               const ProbeResult& probe) noexcept {
    return run_lighting(size, probe, module.id);
}

PipelineResult run_pac_module(const NativeModule& module,
                              std::string_view filename,
                              const std::uint8_t* bytes,
                              std::size_t size,
                              const ProbeResult& probe) noexcept {
    return run_slot_container(filename, bytes, size, probe, module.id);
}

PipelineResult run_pnst_module(const NativeModule& module,
                               std::string_view filename,
                               const std::uint8_t* bytes,
                               std::size_t size,
                               const ProbeResult& probe) noexcept {
    return run_slot_container(filename, bytes, size, probe, module.id);
}

PipelineResult run_nbz_module(const NativeModule& module,
                              std::string_view filename,
                              const std::uint8_t* bytes,
                              std::size_t size,
                              const ProbeResult& probe) noexcept {
    if (size < 4U || bytes == nullptr) {
        return module_support::reject(
            probe, module.id, "NBZ rejected: resource is empty/truncated");
    }
    auto out = structural_pipeline(probe, module.id,
                                   describe_resource(filename, bytes, size, probe));
    out.modules.push_back({"nbz.child-materialization", false});
    return out;
}

}  // namespace

NativeModule dca_module() noexcept {
    return {"formats.dca.record-reader", "DCA", Format::Dca,
            ModuleKind::Structural, false, run_dca_module,
            capability(ResourceCapability::Inspection)};
}
NativeModule lig_module() noexcept {
    return {"formats.lig.record-reader", "LIG", Format::Lig,
            ModuleKind::Structural, false, run_lig_module,
            capability(ResourceCapability::Inspection)};
}
NativeModule lig2_module() noexcept {
    return {"formats.lig2.record-reader", "LIG2", Format::Lig2,
            ModuleKind::Structural, false, run_lig2_module,
            capability(ResourceCapability::Inspection)};
}
NativeModule pac_module() noexcept {
    const auto caps = capability(ResourceCapability::Inspection) |
        ResourceCapability::Container;
    return {"formats.pac.relative-slot-reader", "PAC", Format::Pac,
            ModuleKind::Container, false, run_pac_module, caps};
}
NativeModule pnst_module() noexcept {
    const auto caps = capability(ResourceCapability::Inspection) |
        ResourceCapability::Container;
    return {"formats.pnst.relative-slot-reader", "PNST", Format::Pnst,
            ModuleKind::Container, false, run_pnst_module, caps};
}
NativeModule nbz_module() noexcept {
    const auto caps = capability(ResourceCapability::Inspection) |
        ResourceCapability::Container;
    return {"formats.nbz.container-reader", "NBZ", Format::Nbz,
            ModuleKind::Container, false, run_nbz_module, caps};
}

}  // namespace dmcresource
