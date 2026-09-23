#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "dmc_rengine/formats/mod/transform_domain.hpp"

namespace dmcresource::motion {

// Read-only skeleton authority retained from one canonical MOD parse so that a
// MOT can be bound and evaluated later without reparsing the source bytes.
// Only models whose hierarchy passed the canonical spatial gate get a rig.
struct SkeletonRig final {
    dmc::rengine::formats::mod::transform_domain::ParseResult domain;
    // EXE-confirmed CMotionJoint +0xF8 selector, indexed by MOD node.
    std::vector<std::uint8_t> motion_group_by_node;

    [[nodiscard]] std::size_t node_count() const noexcept {
        return domain.local_transform_records_by_node_index.size();
    }
};

// Build a rig from a parsed MOD transform domain. Returns nullptr when the
// hierarchy is not spatially authoritative (no guessed skeleton).
[[nodiscard]] std::shared_ptr<const SkeletonRig> make_skeleton_rig(
    const dmc::rengine::formats::mod::transform_domain::ParseResult& domain) noexcept;

}  // namespace dmcresource::motion
