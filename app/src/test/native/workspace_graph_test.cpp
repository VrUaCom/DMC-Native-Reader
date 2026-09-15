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
    assert(body_asset != kInvalidAssetId);
    assert(hair_asset != kInvalidAssetId);
    assert(texture_asset != kInvalidAssetId);

    const auto body = graph.add_model_instance(body_asset);
    const auto hair = graph.add_model_instance(hair_asset);
    assert(body != kInvalidInstanceId);
    assert(hair != kInvalidInstanceId);

    const std::vector<InstanceId> targets{body, hair};
    const auto texture_binding = graph.add_binding(
        texture_asset, BindingRole::Texture, targets);
    assert(texture_binding != kInvalidBindingId);
    assert(graph.valid());

    const auto* binding = graph.find_binding(texture_binding);
    assert(binding != nullptr);
    assert(binding->source_asset == texture_asset);
    assert(binding->targets.size() == 2U);
    assert(binding->targets[0] == body);
    assert(binding->targets[1] == hair);

    // Presentation order is not resource identity. Reordering an external view
    // must not alter the graph edge or either stable InstanceId.
    auto reordered = targets;
    std::reverse(reordered.begin(), reordered.end());
    assert(reordered[0] == hair);
    assert(graph.find_instance(body)->model_asset == body_asset);
    assert(graph.find_instance(hair)->model_asset == hair_asset);
    binding = graph.find_binding(texture_binding);
    assert(binding->targets[0] == body);
    assert(binding->targets[1] == hair);

    // Invalid graph mutations fail closed.
    const auto not_model = graph.add_model_instance(texture_asset);
    assert(not_model == kInvalidInstanceId);
    const std::vector<InstanceId> duplicate_targets{body, body};
    assert(graph.add_binding(texture_asset, BindingRole::Texture, duplicate_targets) ==
           kInvalidBindingId);
    const std::vector<InstanceId> missing_target{999999U};
    assert(graph.add_binding(texture_asset, BindingRole::Texture, missing_target) ==
           kInvalidBindingId);
    assert(graph.valid());

    assert(graph.remove_binding(texture_binding));
    assert(graph.find_binding(texture_binding) == nullptr);
    assert(!graph.remove_binding(texture_binding));
    assert(graph.valid());

    return 0;
}
