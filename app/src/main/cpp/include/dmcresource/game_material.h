#pragma once

#include <cstdint>
#include <optional>

// Object material state: turns an object's source flags, its alpha control
// byte and each mesh's four clamp words into the sampler and part-alpha state
// the Reader draws with. Written clean-room from
// docs/cleanroom/specs-v124/game_material.md.
namespace dmcresource::game_material {

// ---- Texture filter -------------------------------------------------------

// Point sampling when source flag bit 0x4000 is set, linear otherwise.
[[nodiscard]] constexpr bool nearest_filter(std::uint32_t source_flags) noexcept {
    return (source_flags & 0x4000u) != 0;
}

// ---- Texture addressing ---------------------------------------------------

// No register when min_u is 0; otherwise the words, unmasked, packed in 64
// bits with 0x0F in the low nibble (U mode bits 0..1, V mode bits 2..3).
[[nodiscard]] constexpr std::optional<std::uint64_t> legacy_clamp_register(
    std::uint16_t min_u, std::uint16_t max_u, std::uint16_t min_v,
    std::uint16_t max_v) noexcept {
    if (min_u == 0) {
        return std::nullopt;
    }
    return (std::uint64_t{min_u} << 4) | 0x0Fu | (std::uint64_t{max_u} << 14) |
           (std::uint64_t{min_v} << 24) | (std::uint64_t{max_v} << 34);
}

enum class Address : std::uint8_t { wrap, clamp };

// Modes 1 and 2 clamp; 0, 3 and anything else wrap.
[[nodiscard]] constexpr Address address_for_mode(unsigned mode) noexcept {
    return (mode == 1 || mode == 2) ? Address::clamp : Address::wrap;
}

struct Addressing {
    Address u{Address::wrap};
    Address v{Address::wrap};
};

// No register: wrap/wrap; otherwise U from register bits 0..1, V from 2..3.
[[nodiscard]] constexpr Addressing addressing(std::uint16_t min_u,
                                              std::uint16_t max_u,
                                              std::uint16_t min_v,
                                              std::uint16_t max_v) noexcept {
    const auto reg = legacy_clamp_register(min_u, max_u, min_v, max_v);
    if (!reg) {
        return {};
    }
    return {address_for_mode(static_cast<unsigned>(*reg & 0x3u)),
            address_for_mode(static_cast<unsigned>((*reg >> 2) & 0x3u))};
}

// ---- Part alpha -----------------------------------------------------------

// A non-zero blend nibble (source_flags & 0xF) forces 0x80; else unchanged.
[[nodiscard]] constexpr std::uint8_t effective_alpha_control(
    std::uint32_t source_flags, std::uint8_t alpha_control) noexcept {
    return (source_flags & 0xFu) != 0 ? std::uint8_t{0x80} : alpha_control;
}

// Above 0x80 gives 1.0; otherwise effective / 255.
[[nodiscard]] constexpr float part_alpha(std::uint8_t effective) noexcept {
    return effective > 0x80 ? 1.0f : static_cast<float>(effective) / 255.0f;
}

// True when the effective control is above 0x80 (part alpha 1.0).
[[nodiscard]] constexpr bool alpha_override(std::uint8_t effective) noexcept {
    return effective > 0x80;
}

// ---- Per-vertex material byte (Mesh::blend0) ------------------------------
// Bits 0..3 blend mode, bit 4 nearest filter, bit 5 alpha override, 6..7 zero.

inline constexpr std::uint8_t kMaterialBlendMask = 0x0F;
inline constexpr std::uint8_t kMaterialNearestBit = 0x10;
inline constexpr std::uint8_t kMaterialOverrideBit = 0x20;

// Blend nibble, plus the nearest-filter and alpha-override bits.
[[nodiscard]] constexpr std::uint8_t material_byte(
    std::uint32_t source_flags, std::uint8_t alpha_control) noexcept {
    std::uint8_t byte = static_cast<std::uint8_t>(source_flags & kMaterialBlendMask);
    if (nearest_filter(source_flags)) {
        byte |= kMaterialNearestBit;
    }
    if (alpha_override(effective_alpha_control(source_flags, alpha_control))) {
        byte |= kMaterialOverrideBit;
    }
    return byte;
}

// The blend mode: the low nibble.
[[nodiscard]] constexpr std::uint8_t material_blend(std::uint8_t byte) noexcept {
    return static_cast<std::uint8_t>(byte & kMaterialBlendMask);
}

// The nearest-filter flag: bit 4.
[[nodiscard]] constexpr bool material_nearest(std::uint8_t byte) noexcept {
    return (byte & kMaterialNearestBit) != 0;
}

// 1.0 with the override bit, otherwise 128/255. An effective control below
// 0x80 is not carried in the byte: it reads back as 128/255.
[[nodiscard]] constexpr float material_part_alpha(std::uint8_t byte) noexcept {
    return (byte & kMaterialOverrideBit) != 0 ? 1.0f : 128.0f / 255.0f;
}

} // namespace dmcresource::game_material
