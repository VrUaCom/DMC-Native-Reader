#include <cassert>
#include <cstdint>
#include <string>
#include <vector>

#include "dmcresource/resource_capabilities.h"
#include "dmcresource/resource_session.h"
#include "dmcresource/spider/model_placement_actions.h"
#include "dmcresource/spider/session_actions.h"

namespace {

using namespace dmcresource;

Matrix4 translated(float x, float y, float z) {
    Matrix4 out{};
    out.values[12] = x;
    out.values[13] = y;
    out.values[14] = z;
    return out;
}

Matrix4 rotated_z_90_translated(float x, float y, float z) {
    Matrix4 out{};
    // Row-vector convention: +X rotates toward +Y.
    out.values[0] = 0.0F;
    out.values[1] = 1.0F;
    out.values[4] = -1.0F;
    out.values[5] = 0.0F;
    out.values[12] = x;
    out.values[13] = y;
    out.values[14] = z;
    return out;
}

Session make_part(const char* name, float base_x, bool with_host_joint) {
    Session session;
    session.probe.format = Format::Mod;
    session.probe.recognized = true;
    session.probe.content_confirmed = true;
    session.probe.family = "MOD";
    session.probe.domain = "model";
    session.probe.support = "read";
    session.probe.evidence = "test";
    session.capabilities =
        capability(ResourceCapability::Inspection) |
        ResourceCapability::Geometry |
        ResourceCapability::SkeletalSkinning;
    session.renderable = true;

    Mesh mesh;
    mesh.vertices = {
        {base_x + 0.0F, 0.0F, 0.0F},
        {base_x + 1.0F, 0.0F, 0.0F},
        {base_x + 0.0F, 1.0F, 0.0F},
    };
    mesh.indices = {0U, 1U, 2U};

    MeshPrimitive primitive;
    primitive.name = std::string{name} + " mesh";
    primitive.mesh = mesh;
    primitive.object_index = 0U;
    primitive.mesh_index = 0U;
    primitive.node_index = -1;
    session.scene.meshes.push_back(primitive);
    session.render_mesh = mesh;
    session.render_triangle_texture_slots = {kNoTextureSlot};

    RenderNode root;
    root.name = std::string{name} + " root";
    root.kind = RenderNodeKind::Bone;
    root.parent = -1;
    root.parent_authority = true;
    root.spatial_authority = true;
    root.local = Matrix4{};
    root.world = Matrix4{};
    session.scene.nodes.push_back(root);

    if (with_host_joint) {
        RenderNode joint;
        joint.name = std::string{name} + " joint";
        joint.kind = RenderNodeKind::Bone;
        joint.parent = 0;
        joint.parent_authority = true;
        joint.spatial_authority = true;
        joint.local = rotated_z_90_translated(10.0F, 5.0F, 0.0F);
        joint.world = rotated_z_90_translated(10.0F, 5.0F, 0.0F);
        session.scene.nodes.push_back(joint);
        session.scene.default_attachment_selector = 1U;
    }

    session.inspection.format = "MOD";
    session.inspection.root.id = std::string{name} + "-document";
    session.inspection.root.title = name;
    session.inspection.root.kind = InspectionKind::Document;
    return session;
}

}  // namespace

int main() {
    namespace actions = dmcresource::spider::actions;
    namespace placement = dmcresource::composite_placement;

    auto body = make_part("body", 0.0F, true);
    auto hair = make_part("hair", 2.0F, false);
    std::vector<const dmcresource::Session*> parts{&body, &hair};
    std::vector<std::string> names{"body.mod", "hair.mod"};

    auto composite = actions::compose_mod_sessions(parts, names);
    assert(composite != nullptr);
    assert(composite->workspace_graph.valid());
    assert(composite->composite_parts.size() == 2U);
    assert(composite->composite_parts[0].instance_id != kInvalidInstanceId);
    assert(composite->composite_parts[1].instance_id != kInvalidInstanceId);
    assert(composite->composite_parts[1].placement.mode ==
           dmcresource::CompositePlacementMode::SourceCoordinates);
    assert(!composite->composite_parts[1].placement.resolved);
    assert(composite->composite_parts[1].placement.host_instance_id == kInvalidInstanceId);
    assert(composite->render_mesh.vertices[3].x == 2.0F);
    assert(composite->render_mesh.vertices[3].y == 0.0F);

    const auto graph_asset_count = composite->workspace_graph.assets().size();
    const auto graph_instance_count = composite->workspace_graph.instances().size();
    const auto graph_binding_count = composite->workspace_graph.bindings().size();
    const auto source_vertex_x = composite->render_mesh.vertices[3].x;
    const auto texture_count = composite->attached_textures.size();
    const auto session_texture_state = composite->texture_companion_attached;
    const auto part_texture_state = composite->composite_parts[1].texture_companion_attached;
    const auto placement_mode_before = composite->composite_parts[1].placement.mode;
    const auto placement_resolved_before = composite->composite_parts[1].placement.resolved;
    const auto placement_host_before =
        composite->composite_parts[1].placement.host_instance_id;
    const auto placement_selector_before =
        composite->composite_parts[1].placement.attachment_selector;

    const auto source_state = session_composite_part_state(composite.get(), 1);
    assert(source_state.has_value());
    assert(source_state->name == "hair.mod");
    assert(source_state->asset_id == composite->composite_parts[1].asset_id);
    assert(source_state->instance_id == composite->composite_parts[1].instance_id);
    assert(!source_state->texture_companion_attached);
    assert(source_state->placement_mode == CompositePlacementMode::SourceCoordinates);
    assert(!source_state->placement_resolved);
    assert(source_state->host_instance_id == kInvalidInstanceId);
    assert(source_state->host_part_index == kNoCompositePart);
    assert(source_state->attachment_selector == kNoAttachmentSelector);
    assert(source_state->host_name.empty());
    assert(source_state->attachment_name.empty());
    assert(!session_composite_part_state(composite.get(), -1).has_value());
    assert(!session_composite_part_state(composite.get(), 99).has_value());
    assert(describe_composite_part_state(composite.get(), 99).empty());
    const auto source_description = describe_composite_part_state(composite.get(), 1);
    assert(source_description.find("instance=") != std::string::npos);
    assert(source_description.find("placement=source-coordinates") != std::string::npos);

    // Read-only state projection must not mutate graph, placement or render data.
    assert(composite->workspace_graph.assets().size() == graph_asset_count);
    assert(composite->workspace_graph.instances().size() == graph_instance_count);
    assert(composite->workspace_graph.bindings().size() == graph_binding_count);
    assert(composite->render_mesh.vertices[3].x == source_vertex_x);
    assert(composite->attached_textures.size() == texture_count);
    assert(composite->texture_companion_attached == session_texture_state);
    assert(composite->composite_parts[1].texture_companion_attached == part_texture_state);
    assert(composite->composite_parts[1].placement.mode == placement_mode_before);
    assert(composite->composite_parts[1].placement.resolved == placement_resolved_before);
    assert(composite->composite_parts[1].placement.host_instance_id ==
           placement_host_before);
    assert(composite->composite_parts[1].placement.attachment_selector ==
           placement_selector_before);

    assert(session_composite_part_node_count(composite.get(), 0) == 2U);
    assert(session_composite_part_node_count(composite.get(), 1) == 1U);
    assert(session_composite_part_node_count(composite.get(), -1) == 0U);
    assert(session_composite_part_node_name(composite.get(), 0, 0) == "body root");
    assert(session_composite_part_node_name(composite.get(), 0, 1) == "body joint");
    assert(session_composite_part_node_name(composite.get(), 0, 99).empty());
    const auto selector =
        session_composite_part_default_attachment_selector(composite.get(), 0);
    assert(selector.has_value());
    assert(*selector == 1U);
    assert(!session_composite_part_default_attachment_selector(
        composite.get(), 1).has_value());

    const auto applied = actions::attach_mod_part_to_host_joint(
        composite.get(), 0U, 1U, 1U);
    assert(applied.ok());
    assert(applied.status == placement::PlacementStatus::Applied);
    assert(composite->composite_parts[1].placement.mode ==
           dmcresource::CompositePlacementMode::HostJoint);
    assert(composite->composite_parts[1].placement.resolved);
    assert(composite->composite_parts[1].placement.host_part_index == 0U);
    assert(composite->composite_parts[1].placement.host_instance_id ==
           composite->composite_parts[0].instance_id);
    assert(composite->composite_parts[1].placement.attachment_selector == 1U);

    // The inspector derives current host vector position from stable InstanceId,
    // not from the placement cache. Deliberately stale the cache before querying.
    composite->composite_parts[1].placement.host_part_index = 1U;
    const auto placed_state = session_composite_part_state(composite.get(), 1);
    assert(placed_state.has_value());
    assert(placed_state->placement_mode == CompositePlacementMode::HostJoint);
    assert(placed_state->placement_resolved);
    assert(placed_state->host_instance_id == composite->composite_parts[0].instance_id);
    assert(placed_state->host_part_index == 0U);
    assert(placed_state->host_name == "body.mod");
    assert(placed_state->attachment_selector == 1U);
    assert(placed_state->attachment_name == "body joint");
    const auto placed_description = describe_composite_part_state(composite.get(), 1);
    assert(placed_description.find("placement=host-joint") != std::string::npos);
    assert(placed_description.find("hostPart=0") != std::string::npos);
    assert(placed_description.find("joint=1") != std::string::npos);

    // This checks matrix order, not only translation. Source (2,0,0) under the
    // host joint (+90 degrees around Z, then +10,+5) becomes (10,7,0).
    assert(composite->render_mesh.vertices[3].x == 10.0F);
    assert(composite->render_mesh.vertices[3].y == 7.0F);

    // Host has two nodes, so child root begins at global node index 2. Child
    // identity world * host joint world must equal the host joint world.
    assert(composite->scene.nodes[2].world.values[0] == 0.0F);
    assert(composite->scene.nodes[2].world.values[1] == 1.0F);
    assert(composite->scene.nodes[2].world.values[4] == -1.0F);
    assert(composite->scene.nodes[2].world.values[5] == 0.0F);
    assert(composite->scene.nodes[2].world.values[12] == 10.0F);
    assert(composite->scene.nodes[2].world.values[13] == 5.0F);
    assert(composite->trace.find("attach-mod-part-to-host-joint") != std::string::npos);

    const auto reset = actions::reset_mod_part_placement(composite.get(), 1U);
    assert(reset.ok());
    assert(reset.status == placement::PlacementStatus::Reset);
    assert(composite->composite_parts[1].placement.mode ==
           dmcresource::CompositePlacementMode::SourceCoordinates);
    assert(!composite->composite_parts[1].placement.resolved);
    assert(composite->composite_parts[1].placement.host_instance_id == kInvalidInstanceId);
    assert(composite->render_mesh.vertices[3].x == 2.0F);
    assert(composite->render_mesh.vertices[3].y == 0.0F);
    assert(composite->scene.nodes[2].world.values[12] == 0.0F);
    assert(composite->scene.nodes[2].world.values[13] == 0.0F);

    const auto reset_state = session_composite_part_state(composite.get(), 1);
    assert(reset_state.has_value());
    assert(reset_state->placement_mode == CompositePlacementMode::SourceCoordinates);
    assert(!reset_state->placement_resolved);
    assert(reset_state->host_instance_id == kInvalidInstanceId);
    assert(reset_state->host_part_index == kNoCompositePart);
    assert(reset_state->attachment_selector == kNoAttachmentSelector);

    const auto invalid_joint = actions::attach_mod_part_to_host_joint(
        composite.get(), 0U, 1U, 99U);
    assert(!invalid_joint.ok());
    assert(invalid_joint.status == placement::PlacementStatus::HostJointUnavailable);

    body.scene.nodes[1].spatial_authority = false;
    auto no_authority = actions::compose_mod_sessions(parts, names);
    assert(no_authority != nullptr);
    const auto rejected = actions::attach_mod_part_to_host_joint(
        no_authority.get(), 0U, 1U, 1U);
    assert(!rejected.ok());
    assert(rejected.status ==
           placement::PlacementStatus::HostJointWithoutSpatialAuthority);

    return 0;
}
