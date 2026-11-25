#include "dmcresource/motion/coat_states.h"

#include <algorithm>
#include <bit>
#include <cmath>

namespace dmcresource::motion {

namespace {
std::uint16_t u16(std::span<const std::uint8_t> b, std::size_t o) noexcept {
    return static_cast<std::uint16_t>(b[o] | (b[o + 1U] << 8U));
}
float f32(std::span<const std::uint8_t> b, std::size_t o) noexcept {
    const std::uint32_t v = static_cast<std::uint32_t>(b[o]) | (static_cast<std::uint32_t>(b[o + 1U]) << 8U) |
                            (static_cast<std::uint32_t>(b[o + 2U]) << 16U) |
                            (static_cast<std::uint32_t>(b[o + 3U]) << 24U);
    return std::bit_cast<float>(v);
}
}  // namespace

std::optional<CoatStates> CoatStates::parse(std::span<const std::uint8_t> bytes) {
    constexpr std::size_t kRecord = 0x14U;
    if (bytes.size() < 8U) return std::nullopt;
    // The entry list ends where the first data begins (the lowest half).
    std::size_t data = bytes.size();
    for (std::size_t o = 0U; o + 4U <= data && o + 4U <= bytes.size(); o += 4U) {
        const std::size_t a = u16(bytes, o);
        const std::size_t b = u16(bytes, o + 2U);
        if (a == 0U || b == 0U) return std::nullopt;
        data = std::min({data, a, b});
    }
    if (data < 8U || data % 4U != 0U || data > 0x200U) return std::nullopt;
    CoatStates out;
    for (std::size_t o = 0U; o < data; o += 4U) {
        CoatStateRecord r;
        r.group = u16(bytes, o);
        r.offset = u16(bytes, o + 2U);
        if (r.offset + kRecord > bytes.size() || r.group + kRecord > bytes.size()) return std::nullopt;
        r.stiffness = f32(bytes, r.offset);
        r.gravity_y = f32(bytes, r.offset + 4U);
        r.wind = {f32(bytes, r.offset + 8U), f32(bytes, r.offset + 12U), f32(bytes, r.offset + 16U)};
        const bool sane = std::isfinite(r.stiffness) && std::fabs(r.stiffness) <= 4.0F && std::isfinite(r.gravity_y) &&
                          std::fabs(r.gravity_y) <= 4.0F &&
                          std::all_of(r.wind.begin(), r.wind.end(),
                                      [](float g) { return std::isfinite(g) && std::fabs(g) <= 4.0F; });
        if (!sane) return std::nullopt;
        out.records_.push_back(r);
    }
    return out;
}

int coat_state_at(std::span<const ScriptSignalKey> keys, float frame) noexcept {
    int state = 0;
    for (const auto& key : keys) {
        if (key.after_frame > frame) break;
        if (key.channels[3] != 0U) state = key.channels[3];
    }
    return state;
}

ClothParams coat_params(const ClothParams& base, const CoatStateRecord& record) noexcept {
    ClothParams out = base;
    out.stiffness = record.stiffness;
    out.gravity = {0.0F, record.gravity_y, 0.0F};
    out.wind = record.wind;
    return out;
}

}  // namespace dmcresource::motion
