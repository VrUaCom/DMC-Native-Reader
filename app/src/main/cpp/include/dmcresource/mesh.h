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

    // Optional COLOR0 (EFM mesh +0x38, RGBA8 with 0x80 = 1.0, PS2 modulate)
    // and per-vertex blend mode of the owning object (source flags & 0xF:
    // 0 opaque, 1/4 alpha, 2 additive, 3 subtractive -- GS ALPHA table
    // 0x1405D0550 via 0x1402F17C0).
    std::vector<std::array<std::uint8_t, 4>> color0;
    std::vector<std::uint8_t> blend0;

    [[nodiscard]] bool has_color0() const noexcept {
        return !color0.empty() && color0.size() == vertices.size();
    }
    [[nodiscard]] bool has_blend0() const noexcept {
        return !blend0.empty() && blend0.size() == vertices.size();
    }
};

struct RgbaImage {
    int width{};
    int height{};
    std::vector<std::uint8_t> pixels; // RGBA8888
};

}  // namespace dmcresource
