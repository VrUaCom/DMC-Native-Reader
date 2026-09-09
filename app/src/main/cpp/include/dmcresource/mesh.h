#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace dmcresource {

struct Vec2 {
    float u{};
    float v{};
};

struct Vec3 {
    float x{};
    float y{};
    float z{};
};

struct Mesh {
    std::vector<Vec3> vertices;
    std::vector<std::uint32_t> indices; // triangles, 3 indices each

    // Frontend-neutral primary UV channel. Format parsers keep authority for
    // serialized UV representation; reusable projection modules populate this
    // only after canonical parsing succeeds.
    std::vector<Vec2> uv0;

    [[nodiscard]] bool has_uv0() const noexcept {
        return !uv0.empty() && uv0.size() == vertices.size();
    }
};

struct RgbaImage {
    int width{};
    int height{};
    std::vector<std::uint8_t> pixels; // RGBA8888
};

}  // namespace dmcresource
