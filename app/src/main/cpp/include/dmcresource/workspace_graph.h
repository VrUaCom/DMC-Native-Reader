#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "dmcresource/cpp23_profile.h"

namespace dmcresource {

using AssetId = std::uint64_t;
using InstanceId = std::uint64_t;
using BindingId = std::uint64_t;

inline constexpr AssetId kInvalidAssetId = 0U;
inline constexpr InstanceId kInvalidInstanceId = 0U;
inline constexpr BindingId kInvalidBindingId = 0U;

enum class ResourceAssetKind : std::uint8_t {
    Unknown,
    Model,
    Texture,
    Motion,
    Physics,
    Cloth,
    Effect,
};

enum class BindingRole : std::uint8_t {
    Texture,
    Motion,
    Physics,
    Cloth,
    Effect,
};

enum class WorkspaceGraphError : std::uint8_t {
    IdExhausted,
    AssetMissing,
    AssetKindMismatch,
    EmptyTargets,
    TargetMissing,
    DuplicateTarget,
    AllocationFailed,
};

template <class T>
using WorkspaceResult = cpp23::Result<T, WorkspaceGraphError>;

// Logical resource identity. label is display/provenance context only; it is
// never used as canonical identity or dependency evidence.
struct ResourceAsset final {
    AssetId id{kInvalidAssetId};
    ResourceAssetKind kind{ResourceAssetKind::Unknown};
    std::string label;
};

// Workspace instance of one model asset. Instance identity remains stable even
// when presentation vectors are reordered or rebuilt.
struct ModelInstance final {
    InstanceId id{kInvalidInstanceId};
    AssetId model_asset{kInvalidAssetId};
};

// Typed relationship from one companion/resource asset to one or more model
// instances. Targets are stable InstanceIds rather than array positions.
struct BindingEdge final {
    BindingId id{kInvalidBindingId};
    AssetId source_asset{kInvalidAssetId};
    BindingRole role{BindingRole::Texture};
    std::vector<InstanceId> targets;
};

// Native ownership graph for assembled resources. This is product/workspace
// authority; flattened render tables and Java URI arrays are derived/lifecycle
// state and must not replace these stable identities. Mutation failures are
// explicit C++23 results rather than ambiguous invalid-ID sentinels.
class WorkspaceGraph final {
public:
    [[nodiscard]] WorkspaceResult<AssetId> add_asset(
        ResourceAssetKind kind, std::string label) noexcept;
    [[nodiscard]] WorkspaceResult<InstanceId> add_model_instance(
        AssetId model_asset) noexcept;
    [[nodiscard]] WorkspaceResult<BindingId> add_binding(
        AssetId source_asset,
        BindingRole role,
        std::span<const InstanceId> targets) noexcept;

    [[nodiscard]] bool remove_binding(BindingId id) noexcept;

    [[nodiscard]] const ResourceAsset* find_asset(AssetId id) const noexcept;
    [[nodiscard]] const ModelInstance* find_instance(InstanceId id) const noexcept;
    [[nodiscard]] const BindingEdge* find_binding(BindingId id) const noexcept;

    [[nodiscard]] const std::vector<ResourceAsset>& assets() const noexcept {
        return assets_;
    }
    [[nodiscard]] const std::vector<ModelInstance>& instances() const noexcept {
        return instances_;
    }
    [[nodiscard]] const std::vector<BindingEdge>& bindings() const noexcept {
        return bindings_;
    }

    [[nodiscard]] bool valid() const noexcept;

private:
    AssetId next_asset_id_{1U};
    InstanceId next_instance_id_{1U};
    BindingId next_binding_id_{1U};
    std::vector<ResourceAsset> assets_;
    std::vector<ModelInstance> instances_;
    std::vector<BindingEdge> bindings_;
};

}  // namespace dmcresource
