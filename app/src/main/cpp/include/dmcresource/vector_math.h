#pragma once
#include <cmath>
#include "dmcresource/mesh.h"
namespace dmcresource::vector_math {
[[nodiscard]] inline Vec3 add(Vec3 a, Vec3 b) noexcept {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}

[[nodiscard]] inline Vec3 sub(Vec3 a, Vec3 b) noexcept {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

[[nodiscard]] inline Vec3 cross(Vec3 a, Vec3 b) noexcept {
    return {
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x,
    };
}

[[nodiscard]] inline float dot(Vec3 a, Vec3 b) noexcept {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

[[nodiscard]] inline Vec3 normalize(Vec3 v) noexcept {
    const float len2 = dot(v, v);
    if (!std::isfinite(len2) || len2 <= 1.0e-12F) return {};
    const float inv = 1.0F / std::sqrt(len2);
    return {v.x * inv, v.y * inv, v.z * inv};
}

}
