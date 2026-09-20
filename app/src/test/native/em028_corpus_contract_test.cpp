#include <cassert>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "dmcresource/resource_capabilities.h"
#include "dmcresource/resource_session.h"

namespace {

dmcresource::Session make_part(
        const char* name,
        float x_offset,
        const std::vector<std::uint32_t>& texture_slots) {
    using namespace dmcresource;

    Session session;
    session.probe.format = Format::Mod;
    session.probe.recognized = true;
    session.probe.content_confirmed = true;
    session.probe.family = "MOD";
    session.probe.domain = "model";
    session.probe.support = "read";
    session.probe.evidence = "em028-corpus-contract";
    session.capabilities =
        capability(ResourceCapability::Inspection) |
        ResourceCapability::Geometry |
        ResourceCapability::Wireframe |
        ResourceCapability::TextureBinding |
        ResourceCapability::UvCoordinates;
    session.renderable = true;

    for (std::size_t mesh_index = 0U;
         mesh_index < texture_slots.size(); ++mesh_index) {
        const float x = x_offset + static_cast<float>(mesh_index) * 2.0F;
        const std::uint32_t vertex_base =
            static_cast<std::uint32_t>(session.render_mesh.vertices.size());

        Mesh local;
        local.vertices = {
            {x + 0.0F, 0.0F, 0.0F},
            {x + 1.0F, 0.0F, 0.0F},
            {x + 0.0F, 1.0F, 0.0F},
        };
        local.indices = {0U, 1U, 2U};
        local.uv0 = {{0.0F, 0.0F}, {1.0F, 0.0F}, {0.0F, 1.0F}};

        MeshPrimitive primitive;
        primitive.name = std::string{name} + " mesh " + std::to_string(mesh_index);
        primitive.mesh = local;
        primitive.object_index = static_cast<std::uint32_t>(mesh_index);
        primitive.mesh_index = 0U;
        primitive.node_index = -1;
        const auto primitive_index =
            static_cast<std::uint32_t>(session.scene.meshes.size());
        session.scene.meshes.push_back(std::move(primitive));
        session.scene.textures.push_back(TextureBinding{
            .mesh_primitive = primitive_index,
            .texture_slot = texture_slots[mesh_index],
            .external_source = "em028_000.ptx",
        });

        session.render_mesh.vertices.insert(
            session.render_mesh.vertices.end(),
            local.vertices.begin(), local.vertices.end());
        session.render_mesh.indices.push_back(vertex_base + 0U);
        session.render_mesh.indices.push_back(vertex_base + 1U);
        session.render_mesh.indices.push_back(vertex_base + 2U);
        session.render_mesh.uv0.insert(
            session.render_mesh.uv0.end(),
            local.uv0.begin(), local.uv0.end());
        session.render_triangle_texture_slots.push_back(texture_slots[mesh_index]);
    }

    RenderNode node;
    node.name = std::string{name} + " root";
    node.kind = RenderNodeKind::Bone;
    node.parent = -1;
    node.parent_authority = true;
    session.scene.nodes.push_back(std::move(node));
    return session;
}

}  // namespace

int main() {
    using namespace dmcresource;

    // Hash-bound owner-supplied em028 corpus, documented in:
    // docs/research/em028-multi-mod-corpus-2026-09-13.md
    //
    // Real root-level mesh texture-slot multiplicity:
    //   em028_001.mod -> 1,1
    //   em028_004.mod -> 0,0,0
    //   em028_005.mod -> 2,2,3,3
    //   em028_006.mod -> 2
    auto body = make_part("em028_001.mod", 0.0F, {1U, 1U});
    auto core = make_part("em028_004.mod", 10.0F, {0U, 0U, 0U});
    auto wings = make_part("em028_005.mod", 20.0F, {2U, 2U, 3U, 3U});
    auto tail = make_part("em028_006.mod", 30.0F, {2U});

    std::vector<const Session*> sources{&body, &core, &wings, &tail};
    std::vector<std::string> names{
        "em028_001.mod", "em028_004.mod", "em028_005.mod", "em028_006.mod"};

    auto composite = compose_mod_sessions(sources, names);
    assert(composite != nullptr);
    assert(composite->composite_parts.size() == 4U);

    assert(composite->composite_parts[0].texture_slot_base == 0U);
    assert(composite->composite_parts[0].texture_slot_span == 2U);
    assert(composite->composite_parts[1].texture_slot_base == 2U);
    assert(composite->composite_parts[1].texture_slot_span == 1U);
    assert(composite->composite_parts[2].texture_slot_base == 3U);
    assert(composite->composite_parts[2].texture_slot_span == 4U);
    assert(composite->composite_parts[3].texture_slot_base == 7U);
    assert(composite->composite_parts[3].texture_slot_span == 3U);

    const std::vector<std::uint32_t> expected_global_slots{
        1U, 1U,
        2U, 2U, 2U,
        5U, 5U, 6U, 6U,
        9U,
    };
    assert(composite->render_triangle_texture_slots == expected_global_slots);

    // One synthetic triangle represents each real mesh only for the remap
    // contract. Real corpus geometry is 5464 vertices / 4083 triangles.
    assert(composite->render_mesh.vertices.size() == 30U);
    assert(composite->render_mesh.indices.size() == 30U);
    assert(composite->render_mesh.uv0.size() == 30U);
    assert(composite->scene.nodes.size() == 4U);

    // Root sibling parts only. MOD children nested below em028_009.pnst belong
    // to the effect domain and are intentionally absent from this contract.
    assert(session_composite_part_name(composite.get(), 0) == "em028_001.mod");
    assert(session_composite_part_name(composite.get(), 1) == "em028_004.mod");
    assert(session_composite_part_name(composite.get(), 2) == "em028_005.mod");
    assert(session_composite_part_name(composite.get(), 3) == "em028_006.mod");

    return 0;
}
