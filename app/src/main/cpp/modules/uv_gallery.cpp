#include "dmcresource/uv_gallery.h"
#include "dmcresource/model_texture_binding.h"
#include <algorithm>

namespace dmcresource {
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
        gallery.maps.push_back({summary.texture_slot, {}});
        gallery.maps.back().indices.reserve(summary.triangle_count * 3U);
    }
    for (std::size_t t = 0; t < triangle_texture_slots.size(); ++t) {
        auto& indices = gallery.maps[group_for_slot[triangle_texture_slots[t]]].indices;
        indices.insert(indices.end(), mesh.indices.begin() + t * 3U,
                       mesh.indices.begin() + (t + 1U) * 3U);
    }
    return gallery;
}
}  // namespace dmcresource
