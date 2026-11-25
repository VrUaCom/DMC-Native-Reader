#pragma once
#include <span>
#include <string>
#include <utility>
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
    // The texture this layout was painted for (the slot's bound texture), in
    // texels; 0 x 0 when no texture is bound and the proportions are unknown.
    // UV space is that texture's rectangle: u runs across its width, v down
    // its height, so a 512 x 256 texture makes a 2:1 drawing.
    std::uint32_t texture_width{};
    std::uint32_t texture_height{};
    // Triangles with a corner outside the 0..1 texture rectangle (tiling).
    std::size_t outside_triangles{};
};
// One UV coordinate snapshot shared by all maps; no position, scene, texture,
// or full-mesh copies per material. Indices occur in exactly one slot group.
struct UvGallery {
    std::vector<Vec2> coordinates;
    std::vector<UvMap> maps;
};
[[nodiscard]] UvGallery build_uv_gallery(const Mesh& mesh,
    std::span<const std::uint32_t> triangle_texture_slots);

// Binds each map to the texture painted on it: `texture_sizes[slot]` is the
// width x height of the texture bound to that slot (0 x 0 when none).
void bind_uv_texture_sizes(UvGallery& gallery,
    std::span<const std::pair<std::uint32_t, std::uint32_t>> texture_sizes);

// The map's proportions as width:height in lowest terms ("2:1", "4:3");
// "1:1 (no texture)" when the texture is unknown.
[[nodiscard]] std::string uv_map_proportion(const UvMap& map);

// Pixel size of a UV map's PNG: the texture's own resolution, scaled by a
// whole factor so the longer side reaches at least 1024 (and never beyond
// 4096), so the drawing lies over the texture texel for texel. Square 1024
// when the texture is unknown.
[[nodiscard]] std::pair<int, int> uv_map_export_size(const UvMap& map);
}  // namespace dmcresource
