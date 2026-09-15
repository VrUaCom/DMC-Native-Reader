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
        joint.local = translated(10.0F, 5.0F, 0.0F);
        joint.world = translated(10.0F, 5.0F, 0.0F);
        session.scene.nodes.push_back(joint);
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
    assert(composite->composite_parts.size() == 2U);
    assert(composite->composite_parts[1].placement.mode ==
           dmcresource::CompositePlacementMode::SourceCoordinates);
    assert(!composite->composite_parts[1].placement.resolved);
    assert(composite->render_mesh.vertices[3].x == 2.0F);
    assert(composite->render_mesh.vertices[3].y == 0.0F);

    const auto applied = actions::attach_mod_part_to_host_joint(
        composite.get(), 0U, 1U, 1U);
    assert(applied.ok());
    assert(applied.status == placement::PlacementStatus::Applied);
    assert(composite->composite_parts[1].placement.mode ==
           dmcresource::CompositePlacementMode::HostJoint);
    assert(composite->composite_parts[1].placement.resolved);
    assert(composite->composite_parts[1].placement.host_part_index == 0U);
    assert(composite->composite_parts[1].placement.attachment_selector == 1U);
    assert(composite->render_mesh.vertices[3].x == 12.0F);
    assert(composite->render_mesh.vertices[3].y == 5.0F);

    // Host has two nodes, so child root begins at global node index 2.
    assert(composite->scene.nodes[2].world.values[12] == 10.0F);
    assert(composite->scene.nodes[2].world.values[13] == 5.0F);
    assert(composite->trace.find("attach-mod-part-to-host-joint") != std::string::npos);

    const auto reset = actions::reset_mod_part_placement(composite.get(), 1U);
    assert(reset.ok());
    assert(reset.status == placement::PlacementStatus::Reset);
    assert(composite->composite_parts[1].placement.mode ==
           dmcresource::CompositePlacementMode::SourceCoordinates);
    assert(!composite->composite_parts[1].placement.resolved);
    assert(composite->render_mesh.vertices[3].x == 2.0F);
    assert(composite->render_mesh.vertices[3].y == 0.0F);
    assert(composite->scene.nodes[2].world.values[12] == 0.0F);
    assert(composite->scene.nodes[2].world.values[13] == 0.0F);

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
