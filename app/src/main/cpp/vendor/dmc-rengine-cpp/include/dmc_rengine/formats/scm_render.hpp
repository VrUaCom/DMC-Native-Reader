#pragma once

#include <cstdint>

namespace dmc::rengine::formats::scm {

struct LegacyGsClampRegionRepeat final {
    std::uint16_t min_u{};
    std::uint16_t max_u{};
    std::uint16_t min_v{};
    std::uint16_t max_v{};
};

inline constexpr std::uint16_t gs_clamp_field_max = 0x03FFU;
inline constexpr std::uint64_t gs_clamp_region_repeat_modes = 0x0FULL;

[[nodiscard]] constexpr bool legacy_gs_clamp_fields_fit_register(
    const LegacyGsClampRegionRepeat& clamp) noexcept {
    return clamp.min_u <= gs_clamp_field_max &&
           clamp.max_u <= gs_clamp_field_max &&
           clamp.min_v <= gs_clamp_field_max &&
           clamp.max_v <= gs_clamp_field_max;
}

[[nodiscard]] constexpr std::uint64_t pack_legacy_gs_clamp_region_repeat(
    const LegacyGsClampRegionRepeat& clamp) noexcept {
    if (clamp.min_u == 0U) return 0U;
    return (static_cast<std::uint64_t>(clamp.min_u) << 4U) |
           gs_clamp_region_repeat_modes |
           (static_cast<std::uint64_t>(clamp.max_u) << 14U) |
           (static_cast<std::uint64_t>(clamp.min_v) << 24U) |
           (static_cast<std::uint64_t>(clamp.max_v) << 34U);
}

inline constexpr std::uint32_t object_flag_nearest_texture_filter = 0x00004000U;
inline constexpr std::uint64_t legacy_gs_tex1_nearest_filter = 0x00U;
inline constexpr std::uint64_t legacy_gs_tex1_linear_filter = 0x60U;

[[nodiscard]] constexpr std::uint64_t legacy_gs_tex1_filter_from_object_flags(
    std::uint32_t object_flags) noexcept {
    return (object_flags & object_flag_nearest_texture_filter) != 0U
        ? legacy_gs_tex1_nearest_filter
        : legacy_gs_tex1_linear_filter;
}

struct AlphaControlProjection final {
    std::uint32_t runtime_control_value{};
    std::uint32_t runtime_override_code{};
    float packet_alpha_w{};
};

inline constexpr std::uint8_t low_mode_forced_alpha_control = 0x80U;
inline constexpr float alpha_byte_scale = 1.0F / 255.0F;

[[nodiscard]] constexpr AlphaControlProjection project_effective_alpha_control(
    std::uint8_t effective_control) noexcept {
    AlphaControlProjection out{};
    out.runtime_control_value = effective_control;
    if (effective_control > 0x80U) {
        out.runtime_override_code = effective_control;
        out.packet_alpha_w = 1.0F;
    } else {
        out.packet_alpha_w = static_cast<float>(effective_control) * alpha_byte_scale;
    }
    return out;
}

} // namespace dmc::rengine::formats::scm
