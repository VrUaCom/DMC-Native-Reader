#include "dmcresource/motion/skeleton_rig.h"

#include <new>

#include "dmc_rengine/analysis/mod/motion_group.hpp"
#include "dmc_rengine/formats/mod/world_transform.hpp"

namespace dmcresource::motion {

std::shared_ptr<const SkeletonRig> make_skeleton_rig(
    const dmc::rengine::formats::mod::transform_domain::ParseResult& domain) noexcept {
    try {
        namespace world = dmc::rengine::formats::mod::world_transform;
        if (!world::supports_spatial_hierarchy(domain)) return nullptr;
        const auto groups = dmc::rengine::analysis::mod::project_motion_groups(domain);
        if (!groups.has_value() ||
            groups->by_node_index.size() != domain.local_transform_records_by_node_index.size()) {
            return nullptr;
        }
        auto rig = std::make_shared<SkeletonRig>();
        rig->domain = domain;
        rig->motion_group_by_node = groups->by_node_index;
        return rig;
    } catch (...) {
        return nullptr;
    }
}

}  // namespace dmcresource::motion
