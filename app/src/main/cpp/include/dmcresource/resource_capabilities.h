#pragma once

#include <cstdint>

namespace dmcresource {

enum class ResourceCapability : std::uint64_t {
    Inspection       = 1ULL << 0U,
    Geometry         = 1ULL << 1U,
    Wireframe        = 1ULL << 2U,
    NodeHierarchy    = 1ULL << 3U,
    SkeletalSkinning = 1ULL << 4U,
    SkinWeights      = 1ULL << 5U,
    TextureBinding   = 1ULL << 6U,
    ImagePreview     = 1ULL << 7U,
    ChildResources   = 1ULL << 8U,
    Text             = 1ULL << 9U,
    Container        = 1ULL << 10U,
    Collision        = 1ULL << 11U,
    Adjacency        = 1ULL << 12U,
    TransformSelectors = 1ULL << 13U,
    UvCoordinates    = 1ULL << 14U,
};

using ResourceCapabilities = std::uint64_t;

[[nodiscard]] constexpr ResourceCapabilities capability(ResourceCapability value) noexcept {
    return static_cast<ResourceCapabilities>(value);
}

[[nodiscard]] constexpr ResourceCapabilities operator|(ResourceCapability left,
                                                        ResourceCapability right) noexcept {
    return capability(left) | capability(right);
}

[[nodiscard]] constexpr ResourceCapabilities operator|(ResourceCapabilities left,
                                                        ResourceCapability right) noexcept {
    return left | capability(right);
}

[[nodiscard]] constexpr bool has_capability(ResourceCapabilities mask,
                                            ResourceCapability value) noexcept {
    return (mask & capability(value)) != 0U;
}

}  // namespace dmcresource
