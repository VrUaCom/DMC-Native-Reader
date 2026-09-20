#include <algorithm>
#include <cassert>
#include <vector>

#include "dmcresource/workspace_graph.h"

int main() {
    using namespace dmcresource;

    WorkspaceGraph graph;
    const auto body_asset = graph.add_asset(ResourceAssetKind::Model, "body.mod");
    const auto hair_asset = graph.add_asset(ResourceAssetKind::Model, "hair.mod");
    const auto texture_asset = graph.add_asset(ResourceAssetKind::Texture, "pl000.ptx");
    assert(body_asset.has_value());
    assert(hair_asset.has_value());
    assert(texture_asset.has_value());

    const auto body = graph.add_model_instance(*body_asset);
    const auto hair = graph.add_model_instance(*hair_asset);
    assert(body.has_value());
    assert(hair.has_value());

    const std::vector<InstanceId> targets{*body, *hair};
    const auto texture_binding = graph.add_binding(
        *texture_asset, BindingRole::Texture, targets);
    assert(texture_binding.has_value());
    assert(graph.valid());

    const auto* binding = graph.find_binding(*texture_binding);
    assert(binding != nullptr);
    assert(binding->source_asset == *texture_asset);
    assert(binding->targets.size() == 2U);
    assert(binding->targets[0] == *body);
    assert(binding->targets[1] == *hair);

    // Presentation order is not resource identity. Reordering an external view
    // must not alter the graph edge or either stable InstanceId.
    auto reordered = targets;
    std::reverse(reordered.begin(), reordered.end());
    assert(reordered[0] == *hair);
    assert(graph.find_instance(*body)->model_asset == *body_asset);
    assert(graph.find_instance(*hair)->model_asset == *hair_asset);
    binding = graph.find_binding(*texture_binding);
    assert(binding->targets[0] == *body);
    assert(binding->targets[1] == *hair);

    // C++23 typed failures keep fail-closed behavior while preserving the exact
    // reason instead of collapsing every failure onto an invalid-ID sentinel.
    const auto not_model = graph.add_model_instance(*texture_asset);
    assert(!not_model.has_value());
    assert(not_model.error() == WorkspaceGraphError::AssetKindMismatch);

    const auto missing_asset = graph.add_model_instance(999999U);
    assert(!missing_asset.has_value());
    assert(missing_asset.error() == WorkspaceGraphError::AssetMissing);

    const std::vector<InstanceId> duplicate_targets{*body, *body};
    const auto duplicate = graph.add_binding(
        *texture_asset, BindingRole::Texture, duplicate_targets);
    assert(!duplicate.has_value());
    assert(duplicate.error() == WorkspaceGraphError::DuplicateTarget);

    const std::vector<InstanceId> missing_target{999999U};
    const auto missing = graph.add_binding(
        *texture_asset, BindingRole::Texture, missing_target);
    assert(!missing.has_value());
    assert(missing.error() == WorkspaceGraphError::TargetMissing);

    const std::vector<InstanceId> no_targets;
    const auto empty = graph.add_binding(
        *texture_asset, BindingRole::Texture, no_targets);
    assert(!empty.has_value());
    assert(empty.error() == WorkspaceGraphError::EmptyTargets);
    assert(graph.valid());

    assert(graph.remove_binding(*texture_binding));
    assert(graph.find_binding(*texture_binding) == nullptr);
    assert(!graph.remove_binding(*texture_binding));
    assert(graph.valid());

    return 0;
}
