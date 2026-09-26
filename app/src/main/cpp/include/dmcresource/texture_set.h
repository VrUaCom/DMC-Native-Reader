#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "dmc_rengine/codecs/dds_bc.hpp"
#include "dmcresource/image_preview.h"

namespace dmcresource::texture_set {

enum class Kind : std::uint8_t {
    invalid = 0,
    standalone_dds,
    wrapped_dds,
    ptx_bundle,
};

struct Slot final {
    std::uint32_t index{};
    std::uint64_t descriptor_offset{};
    std::uint64_t dds_offset{};
    std::uint64_t dds_size{};
    std::uint32_t sector_span{};
    std::uint32_t secondary_width{};
    std::uint32_t secondary_height{};
    dmc::rengine::codecs::dds_bc::Document dds;
};

struct ParseResult final {
    Kind kind{Kind::invalid};
    std::vector<Slot> slots;
    bool ptx_aux_compat_used{};
    // Bundle accepted through the lenient community-tool descriptor path.
    bool ptx_community_descriptors{};
    std::string detail;

    [[nodiscard]] bool ok() const noexcept {
        return kind != Kind::invalid && !slots.empty();
    }
};

// Parse exactly one standalone/descriptor-wrapped DDS source into a neutral
// one-slot texture set. Result/diagnostic construction may allocate; callers
// that expose noexcept ABI/action boundaries must catch and fail closed.
[[nodiscard]] ParseResult parse_dds(
    std::span<const std::byte> source);

// Parse a PTX bundle through the Native Reader compatibility framing path and
// validate every framed DDS child. This remains one framing authority for both
// gallery browsing and model companion attachment. Result/diagnostic storage is
// intentionally throwing-capable below the module/Spider catch boundaries.
[[nodiscard]] ParseResult parse_ptx(
    std::span<const std::byte> source);

[[nodiscard]] const Slot* find_slot(
    const ParseResult& set,
    std::uint32_t index) noexcept;

[[nodiscard]] std::span<const std::byte> dds_bytes(
    std::span<const std::byte> source,
    const Slot& slot) noexcept;

// Decode only the requested slot. Callers decide whether/when to spend RGBA
// memory; parsing and slot validation therefore do not depend on gallery
// preview budgets. RGBA/detail construction may allocate.
[[nodiscard]] bool decode_base_mip(
    std::span<const std::byte> source,
    const Slot& slot,
    ImagePreview* out,
    std::string* detail = nullptr);

}  // namespace dmcresource::texture_set
