#include <cassert>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include "dmcresource/resource_capabilities.h"
#include "dmcresource/resource_session.h"
#include "dmcresource/spider/black_widow.h"

namespace {

dmcresource::Session make_mod_part(const char* name, float x_offset) {
    using namespace dmcresource;

    Session session;
    session.probe.format = Format::Mod;
    session.probe.recognized = true;
    session.probe.content_confirmed = true;
    session.probe.family = "MOD";
    session.probe.domain = "model";
    session.probe.support = "read";
    session.probe.evidence = "test";
    session.probe.mime_type = "application/vnd.dmc.mod";
    session.capabilities =
        capability(ResourceCapability::Inspection) |
        ResourceCapability::Geometry |
        ResourceCapability::Wireframe |
        ResourceCapability::TextureBinding |
        ResourceCapability::UvCoordinates;
    session.renderable = true;

    Mesh mesh;
    mesh.vertices = {
        {x_offset + 0.0F, 0.0F, 0.0F},
        {x_offset + 1.0F, 0.0F, 0.0F},
        {x_offset + 0.0F, 1.0F, 0.0F},
    };
    mesh.indices = {0U, 1U, 2U};
    mesh.uv0 = {{0.0F, 0.0F}, {1.0F, 0.0F}, {0.0F, 1.0F}};

    MeshPrimitive primitive;
    primitive.name = std::string{name} + " mesh";
    primitive.mesh = mesh;
    primitive.object_index = 0U;
    primitive.mesh_index = 0U;
    primitive.node_index = -1;
    session.scene.meshes.push_back(primitive);
    session.scene.textures.push_back({0U, 0U, std::string{name} + ".ptx"});

    RenderNode node;
    node.name = std::string{name} + " root";
    node.kind = RenderNodeKind::Bone;
    node.parent = -1;
    node.parent_authority = true;
    node.spatial_authority = false;
    session.scene.nodes.push_back(node);

    session.render_mesh = mesh;
    session.render_triangle_texture_slots = {0U};

    session.inspection.format = "MOD";
    session.inspection.root.id = std::string{name} + "-document";
    session.inspection.root.title = name;
    session.inspection.root.kind = InspectionKind::Document;
    InspectionNode object;
    object.id = std::string{name} + "-object";
    object.title = std::string{name} + " object";
    object.kind = InspectionKind::Object;
    session.inspection.root.children.push_back(std::move(object));
    return session;
}

}  // namespace

int main() {
    using namespace dmcresource;
    namespace widow = dmcresource::spider::black_widow;

    auto body = make_mod_part("body", 0.0F);
    auto cape = make_mod_part("cape", 10.0F);
    std::vector<const Session*> sources{&body, &cape};
    std::vector<std::string> names{"body.mod", "cape.mod"};

    auto composite = compose_mod_sessions(sources, names);
    assert(composite != nullptr);
    assert(composite->probe.format == Format::Mod);
    assert(composite->renderable);
    assert(composite->composite_parts.size() == 2U);
    assert(session_composite_part_count(composite.get()) == 2U);
    assert(session_composite_part_name(composite.get(), 0) == "body.mod");
    assert(session_composite_part_name(composite.get(), 1) == "cape.mod");

    // Source-local ownership is retained for future animation / physics work.
    assert(composite->composite_parts[0].scene.meshes[0].name == "body mesh");
    assert(composite->composite_parts[1].scene.meshes[0].name == "cape mesh");
    assert(composite->composite_parts[0].texture_slot_base == 0U);
    assert(composite->composite_parts[0].texture_slot_span == 1U);
    assert(composite->composite_parts[1].texture_slot_base == 1U);
    assert(composite->composite_parts[1].texture_slot_span == 1U);

    // The top-level render projection namespaces part names and texture slots
    // without changing source coordinates or inventing cross-part bone links.
    assert(composite->scene.meshes.size() == 2U);
    assert(composite->scene.meshes[0].name == "body.mod / body mesh");
    assert(composite->scene.meshes[1].name == "cape.mod / cape mesh");
    assert(composite->scene.meshes[0].node_index == -1);
    assert(composite->scene.meshes[1].node_index == -1);
    assert(composite->scene.textures.size() == 2U);
    assert(composite->scene.textures[0].texture_slot == 0U);
    assert(composite->scene.textures[1].texture_slot == 1U);
    assert(composite->render_triangle_texture_slots.size() == 2U);
    assert(composite->render_triangle_texture_slots[0] == 0U);
    assert(composite->render_triangle_texture_slots[1] == 1U);
    assert(composite->render_mesh.vertices.size() == 6U);
    assert(composite->render_mesh.indices.size() == 6U);
    assert(composite->render_mesh.vertices[0].x == 0.0F);
    assert(composite->render_mesh.vertices[3].x == 10.0F);

    auto state = black_widow_state(composite.get());
    assert(widow::has_state(state, widow::StateFlag::CanRender));
    assert(widow::has_state(state, widow::StateFlag::CanShowUv));
    assert(widow::has_state(state, widow::StateFlag::TextureCompanionAttachable));
    assert(!widow::has_state(state, widow::StateFlag::TextureCompanionAttached));
    assert(!widow::has_state(state, widow::StateFlag::CanExportPng));

    // Even if the merged scene becomes globally incomplete, a retained part
    // with a complete local binding still keeps explicit per-part PTX attach
    // available. The UV gallery remains correctly disabled for that incomplete
    // merged projection.
    composite->render_triangle_texture_slots[1] =
        std::numeric_limits<std::uint32_t>::max();
    state = black_widow_state(composite.get());
    assert(!widow::has_state(state, widow::StateFlag::CanShowUv));
    assert(widow::has_state(state, widow::StateFlag::TextureCompanionAttachable));
    composite->render_triangle_texture_slots[1] = 1U;

    // A composite PTX must always target one explicit part; global automatic
    // slot matching is intentionally refused because each MOD owns its slots.
    assert(!attach_session_ptx(composite.get(), "unknown.ptx", nullptr, 0U));
    assert(composite->texture_attachment_detail.find("explicit composite MOD part")
           != std::string::npos);

    // Invalid/non-MOD mixtures are rejected rather than silently flattened.
    auto not_mod = cape;
    not_mod.probe.format = Format::Scm;
    sources[1] = &not_mod;
    assert(compose_mod_sessions(sources, names) == nullptr);

    return 0;
}
