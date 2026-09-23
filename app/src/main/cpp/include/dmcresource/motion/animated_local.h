#pragma once

#include <array>
#include <cstdint>

#include "dmc_rengine/formats/mod/world_transform.hpp"

namespace dmcresource::motion {

using Matrix4f = dmc::rengine::formats::mod::world_transform::Matrix4f;

// Current values of the nine CMotionJoint channels (+0x120 .. +0x220, stride
// 0x20) in the EXE's normal channel order: T xyz, R xyz, S xyz.
struct JointChannels final {
    std::array<float, 3> translation{};
    std::array<float, 3> rotation{};
    std::array<float, 3> scale{1.0F, 1.0F, 1.0F};
};

// 0x140310310: rotation channels are quantized through a 16-bit angle before
// the Euler basis is built: cvttss2si(r * 65536/2pi) truncated to int16, then
// multiplied by 2pi/65536. Constants are the exact .rdata values.
inline constexpr float kAngleToInt16 = 10430.376953125F;          // 0x4622F982
inline constexpr float kInt16ToAngle = 9.587380918674171e-05F;    // 0x38C90FDC

// 0x14030E9B0 skips the scale pass while every factor is within this band.
inline constexpr float kUnitScaleLow = 0.9999899864196777F;       // 0x3F7FFF58
inline constexpr float kUnitScaleHigh = 1.0000100135803223F;      // 0x3F800054

[[nodiscard]] float quantize_motion_angle(float radians) noexcept;

// Reconstruct the animated local matrix written to CMotionJoint +0x108 by
// 0x140310310: identity, XYZ Euler basis (0x140330450, same helper as the MOD
// rest pose), translation in row 3, W = 1. Non-unit scale is applied to the
// basis rows (0x14032ED30 convention); the parent-scale compensation inside
// 0x14030E9B0 is not reproduced and is reported as partial by callers.
[[nodiscard]] Matrix4f build_animated_local_matrix(const JointChannels& channels) noexcept;

[[nodiscard]] bool has_non_unit_scale(const JointChannels& channels) noexcept;

}  // namespace dmcresource::motion
