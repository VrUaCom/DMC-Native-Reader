#include "dmcresource/workspace_graph.h"

#include <algorithm>
#include <expected>
#include <limits>
#include <utility>

namespace dmcresource {
namespace {

template <typename Id>
[[nodiscard]] bool can_allocate_id(Id next) noexcept {
    return next != 0U && next != std::numeric_limits<Id>::max();
}

template <typename Collection>
[[nodiscard]] bool unique_nonzero_ids(const Collection& values) noexcept {
    for (std::size_t i = 0U; i < values.size(); ++i) {
        if (values[i].id == 0U) return false;
        for (std::size_t j = i + 1U; j < values.size(); ++j) {
            if (values[i].id == values[j].id) return false;
        }
    }
    return true;
}

}  // namespace

WorkspaceResult<AssetId> WorkspaceGraph::add_asset(
    ResourceAssetKind kind, std::string label) noexcept {
    if (!can_allocate_id(next_asset_id_)) {
        return std::unexpected(WorkspaceGraphError::IdExhausted);
    }
    try {
        const AssetId id = next_asset_id_;
        assets_.push_back(ResourceAsset{
            .id = id,
            .kind = kind,
            .label = std::move(label),
        });
        ++next_asset_id_;
        return id;
    } catch (...) {
        return std::unexpected(WorkspaceGraphError::AllocationFailed);
    }
}

WorkspaceResult<InstanceId> WorkspaceGraph::add_model_instance(
    AssetId model_asset) noexcept {
    const auto* asset = find_asset(model_asset);
    if (asset == nullptr) {
        return std::unexpected(WorkspaceGraphError::AssetMissing);
    }
    if (asset->kind != ResourceAssetKind::Model) {
        return std::unexpected(WorkspaceGraphError::AssetKindMismatch);
    }
    if (!can_allocate_id(next_instance_id_)) {
        return std::unexpected(WorkspaceGraphError::IdExhausted);
    }
    try {
        const InstanceId id = next_instance_id_;
        instances_.push_back(ModelInstance{
            .id = id,
            .model_asset = model_asset,
        });
        ++next_instance_id_;
        return id;
    } catch (...) {
        return std::unexpected(WorkspaceGraphError::AllocationFailed);
    }
}

WorkspaceResult<BindingId> WorkspaceGraph::add_binding(
    AssetId source_asset,
    BindingRole role,
    std::span<const InstanceId> targets) noexcept {
    if (find_asset(source_asset) == nullptr) {
        return std::unexpected(WorkspaceGraphError::AssetMissing);
    }
    if (targets.empty()) {
        return std::unexpected(WorkspaceGraphError::EmptyTargets);
    }
    if (!can_allocate_id(next_binding_id_)) {
        return std::unexpected(WorkspaceGraphError::IdExhausted);
    }

    for (std::size_t i = 0U; i < targets.size(); ++i) {
        if (targets[i] == kInvalidInstanceId || find_instance(targets[i]) == nullptr) {
            return std::unexpected(WorkspaceGraphError::TargetMissing);
        }
        for (std::size_t j = i + 1U; j < targets.size(); ++j) {
            if (targets[i] == targets[j]) {
                return std::unexpected(WorkspaceGraphError::DuplicateTarget);
            }
        }
    }

    try {
        const BindingId id = next_binding_id_;
        BindingEdge edge{
            .id = id,
            .source_asset = source_asset,
            .role = role,
        };
        edge.targets.assign(targets.begin(), targets.end());
        bindings_.push_back(std::move(edge));
        ++next_binding_id_;
        return id;
    } catch (...) {
        return std::unexpected(WorkspaceGraphError::AllocationFailed);
    }
}

bool WorkspaceGraph::remove_binding(BindingId id) noexcept {
    const auto it = std::find_if(
        bindings_.begin(), bindings_.end(),
        [id](const BindingEdge& edge) { return edge.id == id; });
    if (it == bindings_.end()) return false;
    bindings_.erase(it);
    return true;
}

const ResourceAsset* WorkspaceGraph::find_asset(AssetId id) const noexcept {
    if (id == kInvalidAssetId) return nullptr;
    const auto it = std::find_if(
        assets_.begin(), assets_.end(),
        [id](const ResourceAsset& asset) { return asset.id == id; });
    return it == assets_.end() ? nullptr : &*it;
}

const ModelInstance* WorkspaceGraph::find_instance(InstanceId id) const noexcept {
    if (id == kInvalidInstanceId) return nullptr;
    const auto it = std::find_if(
        instances_.begin(), instances_.end(),
        [id](const ModelInstance& instance) { return instance.id == id; });
    return it == instances_.end() ? nullptr : &*it;
}

const BindingEdge* WorkspaceGraph::find_binding(BindingId id) const noexcept {
    if (id == kInvalidBindingId) return nullptr;
    const auto it = std::find_if(
        bindings_.begin(), bindings_.end(),
        [id](const BindingEdge& edge) { return edge.id == id; });
    return it == bindings_.end() ? nullptr : &*it;
}

bool WorkspaceGraph::valid() const noexcept {
    if (!unique_nonzero_ids(assets_) ||
        !unique_nonzero_ids(instances_) ||
        !unique_nonzero_ids(bindings_)) {
        return false;
    }

    for (const auto& instance : instances_) {
        const auto* asset = find_asset(instance.model_asset);
        if (asset == nullptr || asset->kind != ResourceAssetKind::Model) return false;
    }

    for (const auto& binding : bindings_) {
        if (find_asset(binding.source_asset) == nullptr || binding.targets.empty()) {
            return false;
        }
        for (std::size_t i = 0U; i < binding.targets.size(); ++i) {
            if (find_instance(binding.targets[i]) == nullptr) return false;
            for (std::size_t j = i + 1U; j < binding.targets.size(); ++j) {
                if (binding.targets[i] == binding.targets[j]) return false;
            }
        }
    }
    return true;
}

}  // namespace dmcresource
