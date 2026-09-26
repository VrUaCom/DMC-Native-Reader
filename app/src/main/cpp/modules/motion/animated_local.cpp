#include "dmcresource/motion/animated_local.h"

#include <cmath>
#include <cstdint>
#include <limits>

#include "dmc_rengine/formats/mod/transform_domain.hpp"

namespace dmcresource::motion {

float quantize_motion_angle(float radians) noexcept {
    const float scaled = radians * kAngleToInt16;
    // cvttss2si yields the x86 "integer indefinite" 0x80000000 for NaN and for
    // values outside int32; its low 16 bits are 0, i.e. a zero angle.
    std::int32_t truncated = 0;
    if (std::isfinite(scaled) &&
        scaled > static_cast<float>(std::numeric_limits<std::int32_t>::min()) &&
        scaled < static_cast<float>(std::numeric_limits<std::int32_t>::max())) {
        truncated = static_cast<std::int32_t>(scaled);
    }
    const auto wrapped = static_cast<std::int16_t>(
        static_cast<std::uint16_t>(static_cast<std::uint32_t>(truncated) & 0xFFFFU));
    return static_cast<float>(wrapped) * kInt16ToAngle;
}

bool has_non_unit_scale(const JointChannels& channels) noexcept {
    for (const float factor : channels.scale) {
        if (!(factor > kUnitScaleLow && factor < kUnitScaleHigh)) return true;
    }
    return false;
}

Matrix4f build_animated_local_matrix(const JointChannels& channels) noexcept {
    // Reuse the canonical 0x140330450 expansion through the rest-pose builder
    // so rest and animated poses cannot drift apart in axis order or sign.
    dmc::rengine::formats::mod::transform_domain::LocalTransformRecord record{};
    record.translation = {channels.translation[0], channels.translation[1],
                          channels.translation[2]};
    record.rotation_xyz_radians = {quantize_motion_angle(channels.rotation[0]),
                                   quantize_motion_angle(channels.rotation[1]),
                                   quantize_motion_angle(channels.rotation[2])};
    auto local = dmc::rengine::formats::mod::world_transform::build_local_matrix(record);
    local.values[12] = channels.translation[0];
    local.values[13] = channels.translation[1];
    local.values[14] = channels.translation[2];
    local.values[15] = 1.0F;

    if (has_non_unit_scale(channels)) {
        for (std::size_t row = 0U; row < 3U; ++row) {
            for (std::size_t column = 0U; column < 3U; ++column) {
                local.values[row * 4U + column] *= channels.scale[row];
            }
        }
    }
    return local;
}

}  // namespace dmcresource::motion
