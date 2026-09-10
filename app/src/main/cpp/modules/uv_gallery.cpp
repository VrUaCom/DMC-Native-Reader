#include "dmcresource/uv_gallery.h"
#include "dmcresource/model_texture_binding.h"
#include <algorithm>

namespace dmcresource {
UvGallery build_uv_gallery(const Mesh& mesh,
    std::span<const std::uint32_t> triangle_texture_slots) {
    model_texture_binding::RequiredSlots required;
    if (!model_texture_binding::collect_required_slots(
            mesh, triangle_texture_slots, &required)) return {};
    std::sort(required.slots.begin(), required.slots.end());
    UvGallery gallery;
    gallery.coordinates = mesh.uv0;
    std::vector<std::size_t> group_for_slot(required.max_slot + 1U);
    for (const auto slot : required.slots) {
        group_for_slot[slot] = gallery.maps.size();
        gallery.maps.push_back({slot, {}});
    }
    std::vector<std::size_t> counts(gallery.maps.size());
    for (const auto slot : triangle_texture_slots) ++counts[group_for_slot[slot]];
    for (std::size_t i = 0; i < counts.size(); ++i)
        gallery.maps[i].indices.reserve(counts[i] * 3U);
    for (std::size_t t = 0; t < triangle_texture_slots.size(); ++t) {
        auto& indices = gallery.maps[group_for_slot[triangle_texture_slots[t]]].indices;
        indices.insert(indices.end(), mesh.indices.begin() + t * 3U,
                       mesh.indices.begin() + (t + 1U) * 3U);
    }
    return gallery;
}
}  // namespace dmcresource
