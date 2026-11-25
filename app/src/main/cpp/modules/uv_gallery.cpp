#include "dmcresource/uv_gallery.h"
#include "dmcresource/model_texture_binding.h"
#include <algorithm>
#include <cmath>
#include <numeric>

namespace dmcresource {
namespace {
constexpr int kExportMinSide = 1024;
constexpr int kExportMaxSide = 4096;

[[nodiscard]] bool inside_texture(const Vec2& uv) noexcept {
    // A hair of tolerance: exporters write 1.0 as 1.0000001.
    constexpr float kEpsilon = 1.0e-4F;
    return uv.u >= -kEpsilon && uv.u <= 1.0F + kEpsilon &&
           uv.v >= -kEpsilon && uv.v <= 1.0F + kEpsilon;
}
}  // namespace

std::vector<UvMapSummary> summarize_uv_maps(const Mesh& mesh,
    std::span<const std::uint32_t> triangle_texture_slots) {
    model_texture_binding::RequiredSlots required;
    if (!model_texture_binding::collect_required_slots(
            mesh, triangle_texture_slots, &required)) return {};
    std::sort(required.slots.begin(), required.slots.end());
    std::vector<std::size_t> counts(required.max_slot + 1U);
    for (const auto slot : triangle_texture_slots) ++counts[slot];
    std::vector<UvMapSummary> result;
    result.reserve(required.slots.size());
    for (const auto slot : required.slots) result.push_back({slot, counts[slot]});
    return result;
}

UvGallery build_uv_gallery(const Mesh& mesh,
    std::span<const std::uint32_t> triangle_texture_slots) {
    const auto summaries = summarize_uv_maps(mesh, triangle_texture_slots);
    if (summaries.empty()) return {};
    UvGallery gallery;
    gallery.coordinates = mesh.uv0;
    std::vector<std::size_t> group_for_slot(summaries.back().texture_slot + 1U);
    for (const auto& summary : summaries) {
        group_for_slot[summary.texture_slot] = gallery.maps.size();
        gallery.maps.push_back({.texture_slot = summary.texture_slot});
        gallery.maps.back().indices.reserve(summary.triangle_count * 3U);
    }
    for (std::size_t t = 0; t < triangle_texture_slots.size(); ++t) {
        auto& map = gallery.maps[group_for_slot[triangle_texture_slots[t]]];
        const auto first = mesh.indices.begin() + static_cast<std::ptrdiff_t>(t * 3U);
        map.indices.insert(map.indices.end(), first, first + 3);
        if (!inside_texture(mesh.uv0[first[0]]) || !inside_texture(mesh.uv0[first[1]]) ||
            !inside_texture(mesh.uv0[first[2]])) {
            ++map.outside_triangles;
        }
    }
    return gallery;
}

void bind_uv_texture_sizes(UvGallery& gallery,
    std::span<const std::pair<std::uint32_t, std::uint32_t>> texture_sizes) {
    for (auto& map : gallery.maps) {
        if (map.texture_slot >= texture_sizes.size()) continue;
        const auto [width, height] = texture_sizes[map.texture_slot];
        if (width == 0U || height == 0U) continue;
        map.texture_width = width;
        map.texture_height = height;
    }
}

std::string uv_map_proportion(const UvMap& map) {
    if (map.texture_width == 0U || map.texture_height == 0U) return "1:1 (no texture)";
    const auto divisor = std::gcd(map.texture_width, map.texture_height);
    return std::to_string(map.texture_width / divisor) + ":" +
           std::to_string(map.texture_height / divisor);
}

std::pair<int, int> uv_map_export_size(const UvMap& map) {
    if (map.texture_width == 0U || map.texture_height == 0U) {
        return {kExportMinSide, kExportMinSide};
    }
    const auto longer = std::max(map.texture_width, map.texture_height);
    if (longer > static_cast<std::uint32_t>(kExportMaxSide)) {
        // Larger than the export cap: keep the exact ratio, shrink to fit.
        const double scale = static_cast<double>(kExportMaxSide) / static_cast<double>(longer);
        return {std::max(1, static_cast<int>(std::lround(map.texture_width * scale))),
                std::max(1, static_cast<int>(std::lround(map.texture_height * scale)))};
    }
    // A whole factor keeps one texel = N x N pixels, so the PNG lies over the
    // texture without resampling seams.
    std::uint32_t factor = 1U;
    while (longer * factor < static_cast<std::uint32_t>(kExportMinSide) &&
           longer * (factor + 1U) <= static_cast<std::uint32_t>(kExportMaxSide)) {
        ++factor;
    }
    return {static_cast<int>(map.texture_width * factor),
            static_cast<int>(map.texture_height * factor)};
}
}  // namespace dmcresource
