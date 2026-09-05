#include "dmcresource/native_module.h"

#include <cstddef>
#include <cstdint>
#include <sstream>
#include <string>

#include "dmcresource/binary_reader.h"

namespace dmcresource {
namespace {

PipelineResult reject(const ProbeResult& probe, const char* id,
                      const char* detail) noexcept {
    PipelineResult out;
    out.probe = probe;
    out.accepted = false;
    out.detail = detail;
    out.modules.push_back({"identity-probe", true});
    out.modules.push_back({"bounded-read-guard", true});
    out.modules.push_back({id, false});
    return out;
}

bool magic4(const BinaryReader& reader, char a, char b, char c, char d) noexcept {
    const auto* p = reader.ptr(0u, 4u);
    return p != nullptr && p[0] == static_cast<std::uint8_t>(a) &&
           p[1] == static_cast<std::uint8_t>(b) &&
           p[2] == static_cast<std::uint8_t>(c) &&
           p[3] == static_cast<std::uint8_t>(d);
}

PipelineResult run_dca(std::string_view,
                       const std::uint8_t* bytes,
                       std::size_t size,
                       const ProbeResult& probe,
                       const char* id) noexcept {
    constexpr std::size_t header = 0x10u;
    constexpr std::size_t record = 0x410u;
    const BinaryReader reader(bytes, size);
    if (size < header || !magic4(reader, 'D', 'C', 'A', '\0') ||
        (size - header) % record != 0u) {
        return reject(probe, id,
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
    constexpr std::size_t header = 0x20u;
    constexpr std::size_t record = 0x30u;
    if (size < header || (size - header) % record != 0u) {
        return reject(probe, id,
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
    std::uint32_t slots = 0u;
    if (size < 8u || !reader.read_le(4u, &slots) ||
        slots > (size - 8u) / 4u) {
        return reject(probe, id, "relative-slot container header/table is invalid");
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
    if (size < 4u || bytes == nullptr) {
        return reject(probe, module.id,
                      "NBZ rejected: resource is empty/truncated");
    }
    auto out = structural_pipeline(probe, module.id,
                                   describe_resource(filename, bytes, size, probe));
    out.modules.push_back({"nbz.child-materialization", false});
    return out;
}

}  // namespace

NativeModule dca_module() noexcept {
    return {"formats.dca.record-reader", "DCA", Format::Dca,
            ModuleKind::Structural, false, run_dca_module};
}
NativeModule lig_module() noexcept {
    return {"formats.lig.record-reader", "LIG", Format::Lig,
            ModuleKind::Structural, false, run_lig_module};
}
NativeModule lig2_module() noexcept {
    return {"formats.lig2.record-reader", "LIG2", Format::Lig2,
            ModuleKind::Structural, false, run_lig2_module};
}
NativeModule pac_module() noexcept {
    return {"formats.pac.relative-slot-reader", "PAC", Format::Pac,
            ModuleKind::Container, false, run_pac_module};
}
NativeModule pnst_module() noexcept {
    return {"formats.pnst.relative-slot-reader", "PNST", Format::Pnst,
            ModuleKind::Container, false, run_pnst_module};
}
NativeModule nbz_module() noexcept {
    return {"formats.nbz.container-reader", "NBZ", Format::Nbz,
            ModuleKind::Container, false, run_nbz_module};
}

}  // namespace dmcresource
