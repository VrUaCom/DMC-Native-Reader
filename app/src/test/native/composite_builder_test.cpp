#include <cassert>
#include <optional>
#include <string>
#include <vector>

#include "dmcresource/composite_builder.h"
#include "dmcresource/resource_capabilities.h"

namespace {

using namespace dmcresource;

Matrix4 translated(float x, float y, float z) {
    Matrix4 out{};
    out.values[12] = x;
    out.values[13] = y;
    out.values[14] = z;
    return out;
}

Session make_part(const char* name,
                  float base_x,
                  bool host_skeleton,
                  std::optional<std::uint32_t> selector) {
    Session session;
    session.probe.format = Format::Mod;
    session.probe.recognized = true;
    session.probe.content_confirmed = true;
    session.probe.family = "MOD";
    session.probe.domain = "model";
    session.probe.support = "read";
    session.probe.evidence = "test";
    session.capabilities = capability(ResourceCapability::Geometry) |
        ResourceCapability::NodeHierarchy;
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
    session.scene.default_attachment_selector = selector;
    session.render_mesh = mesh;
    session.render_triangle_texture_slots = {kNoTextureSlot};

    RenderNode root;
    root.name = std::string{name} + " root";
    root.kind = RenderNodeKind::Bone;
    root.parent = -1;
    root.parent_authority = true;
    root.spatial_authority = true;
    root.world = Matrix4{};
    session.scene.nodes.push_back(root);

    if (host_skeleton) {
        RenderNode joint;
        joint.name = std::string{name} + " head-joint";
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
    namespace builder = dmcresource::composite_builder;

    auto body = make_part("body", 0.0F, true, std::nullopt);
    auto hair = make_part("hair", 2.0F, false, 1U);
    std::vector<const dmcresource::Session*> parts{&body, &hair};
    std::vector<std::string> names{"body.mod", "hair.mod"};

    auto built = builder::build_mod_composite(parts, names);
    assert(built);
    assert(built.stats.attachment_attempts == 1U);
    assert(built.stats.attachments_resolved == 1U);
    assert(built.stats.attachments_unresolved == 0U);
    assert(built.session->composite_parts[1].placement.resolved);
    assert(built.session->composite_parts[1].placement.host_part_index == 0U);
    assert(built.session->composite_parts[1].placement.attachment_selector == 1U);
    assert(built.session->render_mesh.vertices[3].x == 12.0F);
    assert(built.session->render_mesh.vertices[3].y == 5.0F);

    // Source-local child geometry remains authority and is not rewritten.
    assert(built.session->composite_parts[1].scene.meshes[0].mesh.vertices[0].x == 2.0F);
    assert(built.session->composite_parts[1].scene.meshes[0].mesh.vertices[0].y == 0.0F);

    auto invalid_hair = make_part("hair-invalid", 2.0F, false, 99U);
    parts[1] = &invalid_hair;
    auto unresolved = builder::build_mod_composite(parts, names);
    assert(unresolved);
    assert(unresolved.stats.attachment_attempts == 1U);
    assert(unresolved.stats.attachments_resolved == 0U);
    assert(unresolved.stats.attachments_unresolved == 1U);
    assert(!unresolved.session->composite_parts[1].placement.resolved);
    assert(unresolved.session->render_mesh.vertices[3].x == 2.0F);
    assert(unresolved.session->render_mesh.vertices[3].y == 0.0F);

    parts[1] = &hair;
    auto source_only = builder::build_mod_composite(
        parts, names,
        builder::BuildOptions{
            .primary_host_index = 0U,
            .resolve_default_joint_attachments = false,
        });
    assert(source_only);
    assert(source_only.stats.attachment_attempts == 0U);
    assert(!source_only.session->composite_parts[1].placement.resolved);
    assert(source_only.session->render_mesh.vertices[3].x == 2.0F);

    return 0;
}
