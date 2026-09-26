#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "dmcresource/dmc_resource.h"

namespace dmcresource::archive {

// Identity of one payload found inside a DMC container. Byte magic wins; PTX
// (no magic) is accepted only when the canonical texture-slot framing parses.
struct EntryKind final {
    Format format{Format::Unknown};
    const char* family{"BIN"};
    const char* extension{"bin"};
    bool shadow{false};       // SHW: shadow volume records
};

[[nodiscard]] EntryKind classify_payload(const std::uint8_t* bytes, std::size_t size) noexcept;

[[nodiscard]] std::string slot_filename(std::uint32_t slot, const EntryKind& kind);

}  // namespace dmcresource::archive
