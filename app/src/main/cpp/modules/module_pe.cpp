#include "dmcresource/native_module.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

#include "dmcresource/binary_reader.h"

namespace dmcresource {
namespace {

constexpr std::size_t kDosHeaderBytes = 0x40u;
constexpr std::size_t kCoffEnvelopeBytes = 24u;
constexpr std::size_t kSectionHeaderBytes = 40u;
constexpr std::uint16_t kMaxSections = 4096u;

struct Section final {
    std::string name;
    std::uint32_t virtual_size{};
    std::uint32_t virtual_address{};
    std::uint32_t raw_size{};
    std::uint32_t raw_offset{};
};

std::string section_name(const BinaryReader& reader, std::size_t offset) {
    const auto* bytes = reader.ptr(offset, 8u);
    if (bytes == nullptr) return {};
    std::string out;
    for (std::size_t i = 0; i < 8u && bytes[i] != 0u; ++i) {
        out.push_back(static_cast<char>(bytes[i]));
    }
    return out;
}

PipelineResult reject(const ProbeResult& probe,
                      const NativeModule& module,
                      const char* reason) noexcept {
    PipelineResult out;
    out.probe = probe;
    out.accepted = false;
    out.renderable = false;
    out.detail = reason;
    out.modules.push_back({"identity-probe", true});
    out.modules.push_back({"bounded-read-guard", true});
    out.modules.push_back({module.id, false});
    return out;
}

PipelineResult run_pe(const NativeModule& module,
                      std::string_view,
                      const std::uint8_t* bytes,
                      std::size_t size,
                      const ProbeResult& probe) noexcept {
    const BinaryReader reader(bytes, size);
    if (!reader.range(0u, kDosHeaderBytes)) {
        return reject(probe, module, "PE rejected: file is too small for DOS header");
    }

    std::uint16_t dos_magic = 0u;
    std::uint32_t pe_offset32 = 0u;
    if (!reader.read_le(0u, &dos_magic) || dos_magic != 0x5A4Du ||
        !reader.read_le(0x3Cu, &pe_offset32)) {
        return reject(probe, module, "PE rejected: invalid MZ/e_lfanew header");
    }

    const auto pe_offset = static_cast<std::size_t>(pe_offset32);
    if (!reader.range(pe_offset, kCoffEnvelopeBytes)) {
        return reject(probe, module, "PE rejected: PE header points outside file");
    }

    std::uint32_t signature = 0u;
    std::uint16_t machine = 0u;
    std::uint16_t section_count = 0u;
    std::uint16_t optional_size16 = 0u;
    if (!reader.read_le(pe_offset, &signature) || signature != 0x00004550u ||
        !reader.read_le(pe_offset + 4u, &machine) ||
        !reader.read_le(pe_offset + 6u, &section_count) ||
        !reader.read_le(pe_offset + 20u, &optional_size16)) {
        return reject(probe, module, "PE rejected: invalid/truncated PE or COFF header");
    }
    if (section_count > kMaxSections) {
        return reject(probe, module, "PE rejected: section count exceeds safety limit");
    }

    const auto optional_offset = pe_offset + 24u;
    const auto optional_size = static_cast<std::size_t>(optional_size16);
    if (optional_size < 70u || !reader.range(optional_offset, optional_size)) {
        return reject(probe, module, "PE rejected: truncated/unsupported optional header");
    }

    std::uint16_t optional_magic = 0u;
    std::uint32_t entry_point = 0u;
    std::uint32_t size_of_image = 0u;
    std::uint32_t size_of_headers = 0u;
    std::uint16_t subsystem = 0u;
    std::uint64_t image_base = 0u;
    const char* kind = nullptr;
    if (!reader.read_le(optional_offset, &optional_magic) ||
        !reader.read_le(optional_offset + 16u, &entry_point) ||
        !reader.read_le(optional_offset + 56u, &size_of_image) ||
        !reader.read_le(optional_offset + 60u, &size_of_headers) ||
        !reader.read_le(optional_offset + 68u, &subsystem)) {
        return reject(probe, module, "PE rejected: required optional-header fields are truncated");
    }

    if (optional_magic == 0x10Bu) {
        std::uint32_t base32 = 0u;
        if (!reader.read_le(optional_offset + 28u, &base32)) {
            return reject(probe, module, "PE rejected: truncated PE32 image base");
        }
        image_base = base32;
        kind = "PE32";
    } else if (optional_magic == 0x20Bu) {
        if (!reader.read_le(optional_offset + 24u, &image_base)) {
            return reject(probe, module, "PE rejected: truncated PE32+ image base");
        }
        kind = "PE32+";
    } else {
        return reject(probe, module, "PE rejected: unsupported optional-header magic");
    }

    const auto section_table = optional_offset + optional_size;
    if (!reader.table(section_table, section_count, kSectionHeaderBytes)) {
        return reject(probe, module, "PE rejected: truncated section table");
    }

    std::vector<Section> sections;
    sections.reserve(section_count);
    std::size_t virtual_overflow_warnings = 0u;
    for (std::size_t index = 0u; index < section_count; ++index) {
        const auto offset = section_table + index * kSectionHeaderBytes;
        Section section;
        section.name = section_name(reader, offset);
        if (!reader.read_le(offset + 8u, &section.virtual_size) ||
            !reader.read_le(offset + 12u, &section.virtual_address) ||
            !reader.read_le(offset + 16u, &section.raw_size) ||
            !reader.read_le(offset + 20u, &section.raw_offset)) {
            return reject(probe, module, "PE rejected: truncated section header");
        }

        const std::uint64_t raw_end = static_cast<std::uint64_t>(section.raw_offset) +
                                      static_cast<std::uint64_t>(section.raw_size);
        if (raw_end > static_cast<std::uint64_t>(size)) {
            return reject(probe, module, "PE rejected: section raw data exceeds file boundary");
        }

        const std::uint64_t virtual_end =
            static_cast<std::uint64_t>(section.virtual_address) +
            static_cast<std::uint64_t>(std::max(section.virtual_size, section.raw_size));
        if (virtual_end > static_cast<std::uint64_t>(size_of_image)) {
            ++virtual_overflow_warnings;
        }
        sections.push_back(std::move(section));
    }

    std::size_t overlap_warnings = 0u;
    for (std::size_t left = 0u; left < sections.size(); ++left) {
        if (sections[left].raw_size == 0u) continue;
        const std::uint64_t first_begin = sections[left].raw_offset;
        const std::uint64_t first_end = first_begin + sections[left].raw_size;
        for (std::size_t right = left + 1u; right < sections.size(); ++right) {
            if (sections[right].raw_size == 0u) continue;
            const std::uint64_t second_begin = sections[right].raw_offset;
            const std::uint64_t second_end = second_begin + sections[right].raw_size;
            if (first_begin < second_end && second_begin < first_end) ++overlap_warnings;
        }
    }

    const char* machine_name = "unknown";
    if (machine == 0x014Cu) machine_name = "i386";
    else if (machine == 0x8664u) machine_name = "amd64";
    else if (machine == 0xAA64u) machine_name = "arm64";

    std::ostringstream detail;
    detail << "PE structural image | kind=" << kind
           << " machine=" << machine_name
           << " sections=" << section_count
           << " imageBase=0x" << std::hex << image_base
           << " entryRva=0x" << entry_point
           << " sizeOfImage=0x" << size_of_image
           << " sizeOfHeaders=0x" << size_of_headers
           << std::dec << " subsystem=" << subsystem
           << " warnings=" << (virtual_overflow_warnings + overlap_warnings);

    auto out = structural_pipeline(probe, module.id, detail.str());
    out.modules.insert(out.modules.begin() + 2, {"pe.dos-coff-optional-header", true});
    out.modules.insert(out.modules.begin() + 3, {"pe.section-table-bounds", true});
    return out;
}

}  // namespace

NativeModule pe_module() noexcept {
    return {"exe.pe-reader", "PE", Format::Pe,
            ModuleKind::Structural, false, run_pe};
}

}  // namespace dmcresource
