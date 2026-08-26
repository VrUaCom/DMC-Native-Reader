#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace dmcresource {

struct Vec3 {
    float x{};
    float y{};
    float z{};
};

struct Mesh {
    std::vector<Vec3> vertices;
    std::vector<std::uint32_t> indices; // triangles, 3 indices each
};

struct RgbaImage {
    int width{};
    int height{};
    std::vector<std::uint8_t> pixels; // RGBA8888
};

}  // namespace dmcresource
