#pragma once

#include <array>
#include <cstdint>
#include <limits>
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
    static constexpr std::uint32_t kNoTextureSlot =
        std::numeric_limits<std::uint32_t>::max();

    std::vector<Vec3> vertices;
    std::vector<std::uint32_t> indices; // triangles, 3 indices each

    // Frontend-neutral primary UV channel. Format parsers keep authority for
    // serialized UV representation; reusable projection modules populate this
    // only after canonical parsing succeeds.
    std::vector<Vec2> uv0;

    // Frontend-neutral material projection. One entry per rendered triangle;
    // values are the canonical RenderScene texture-slot bindings. This is
    // populated only while materializing RenderScene and lets the renderer
    // select a companion texture without knowing whether the source was MOD,
    // SCM, or another future model family.
    std::vector<std::uint32_t> triangle_texture_slots;

    [[nodiscard]] bool has_uv0() const noexcept {
        return !uv0.empty() && uv0.size() == vertices.size();
    }

    [[nodiscard]] bool has_triangle_texture_slots() const noexcept {
        return !indices.empty() && indices.size() % 3U == 0U &&
               triangle_texture_slots.size() == indices.size() / 3U;
    }
};

struct RgbaImage {
    int width{};
    int height{};
    std::vector<std::uint8_t> pixels; // RGBA8888
};

}  // namespace dmcresource
