#pragma once

#include <cmath>
#include <cstddef>

#include "dmcresource/render_scene.h"

namespace dmcresource::matrix_ops {

[[nodiscard]] inline bool is_finite_affine(const Matrix4& matrix) noexcept {
    for (const float value : matrix.values) {
        if (!std::isfinite(value)) return false;
    }
    return std::fabs(matrix.values[3]) <= 0.0001F &&
           std::fabs(matrix.values[7]) <= 0.0001F &&
           std::fabs(matrix.values[11]) <= 0.0001F &&
           std::fabs(matrix.values[15] - 1.0F) <= 0.0001F;
}

[[nodiscard]] inline bool transform_point(const Vec3& source,
                                          const Matrix4& matrix,
                                          Vec3* out) noexcept {
    if (out == nullptr || !is_finite_affine(matrix)) return false;
    const auto& m = matrix.values;
    const float x = source.x * m[0] + source.y * m[4] +
                    source.z * m[8] + m[12];
    const float y = source.x * m[1] + source.y * m[5] +
                    source.z * m[9] + m[13];
    const float z = source.x * m[2] + source.y * m[6] +
                    source.z * m[10] + m[14];
    const float w = source.x * m[3] + source.y * m[7] +
                    source.z * m[11] + m[15];
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z) ||
        !std::isfinite(w) || std::fabs(w - 1.0F) > 0.0001F) {
        return false;
    }
    *out = {x, y, z};
    return true;
}

[[nodiscard]] inline bool multiply(const Matrix4& left,
                                   const Matrix4& right,
                                   Matrix4* out) noexcept {
    if (out == nullptr || !is_finite_affine(left) || !is_finite_affine(right)) {
        return false;
    }
    Matrix4 result{};
    for (std::size_t row = 0U; row < 4U; ++row) {
        for (std::size_t col = 0U; col < 4U; ++col) {
            float value = 0.0F;
            for (std::size_t k = 0U; k < 4U; ++k) {
                value += left.values[row * 4U + k] * right.values[k * 4U + col];
            }
            if (!std::isfinite(value)) return false;
            result.values[row * 4U + col] = value;
        }
    }
    if (!is_finite_affine(result)) return false;
    *out = result;
    return true;
}

[[nodiscard]] inline Vec3 translation(const Matrix4& matrix) noexcept {
    return {matrix.values[12], matrix.values[13], matrix.values[14]};
}

}  // namespace dmcresource::matrix_ops
