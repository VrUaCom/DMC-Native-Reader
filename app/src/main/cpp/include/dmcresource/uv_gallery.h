#pragma once
#include <span>
#include <vector>
#include "dmcresource/mesh.h"

namespace dmcresource {
struct UvMapSummary {
    std::uint32_t texture_slot{};
    std::size_t triangle_count{};
};
[[nodiscard]] std::vector<UvMapSummary> summarize_uv_maps(const Mesh& mesh,
    std::span<const std::uint32_t> triangle_texture_slots);

struct UvMap {
    std::uint32_t texture_slot{};
    std::vector<std::uint32_t> indices;
};
// One UV coordinate snapshot shared by all maps; no position, scene, texture,
// or full-mesh copies per material. Indices occur in exactly one slot group.
struct UvGallery {
    std::vector<Vec2> coordinates;
    std::vector<UvMap> maps;
};
[[nodiscard]] UvGallery build_uv_gallery(const Mesh& mesh,
    std::span<const std::uint32_t> triangle_texture_slots);
}  // namespace dmcresource
