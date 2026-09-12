#include <algorithm>
#include <bit>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include "dmcresource/resource_capabilities.h"
#include "dmcresource/resource_session.h"
#include "dmcresource/spider/black_widow.h"

namespace {

void put_u16(std::vector<std::uint8_t>& bytes,
             std::size_t offset,
             std::uint16_t value) {
    assert(offset + 2U <= bytes.size());
    bytes[offset + 0U] = static_cast<std::uint8_t>(value & 0xFFU);
    bytes[offset + 1U] = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
}

void put_u32(std::vector<std::uint8_t>& bytes,
             std::size_t offset,
             std::uint32_t value) {
    assert(offset + 4U <= bytes.size());
    for (std::size_t i = 0U; i < 4U; ++i) {
        bytes[offset + i] = static_cast<std::uint8_t>(
            (value >> (i * 8U)) & 0xFFU);
    }
}

std::vector<std::uint8_t> make_shared_four_slot_ptx() {
    constexpr std::uint32_t width = 4U;
    constexpr std::uint32_t height = 4U;
    constexpr std::uint32_t mip_count = 3U;
    constexpr std::size_t dds_size = 128U + 8U * mip_count;

    std::vector<std::uint8_t> dds(dds_size, 0U);
    dds[0] = 'D'; dds[1] = 'D'; dds[2] = 'S'; dds[3] = ' ';
    put_u32(dds, 4U, 124U);
    put_u32(dds, 8U, 0x000A1007U);
    put_u32(dds, 12U, height);
    put_u32(dds, 16U, width);
    put_u32(dds, 20U, 8U);
    put_u32(dds, 28U, mip_count);
    put_u32(dds, 76U, 32U);
    put_u32(dds, 80U, 4U);
    dds[84] = 'D'; dds[85] = 'X'; dds[86] = 'T'; dds[87] = '1';
    put_u32(dds, 108U, 0x00401008U);
    put_u16(dds, 128U, 0xF800U);
    put_u16(dds, 130U, 0x07E0U);
    put_u32(dds, 132U, 0U);

    std::vector<std::uint8_t> descriptor(0x70U, 0U);
    put_u32(descriptor, 0x08U, 0x20000U | (mip_count << 8U) | 0x86U);
    put_u32(descriptor, 0x0CU, 0xAAE4U);
    put_u32(descriptor, 0x10U, (height << 16U) | width);
    put_u32(descriptor, 0x14U, 1U);
    put_u32(descriptor, 0x18U, width * 2U);
    put_u32(descriptor, 0x20U, 0x40U);
    put_u32(descriptor, 0x38U, static_cast<std::uint32_t>(dds.size() - 128U));
    put_u32(descriptor, 0x3CU, 2U);
    put_u32(descriptor, 0x40U, 1U);
    put_u32(descriptor, 0x44U, (height << 16U) | width);
    put_u32(descriptor, 0x48U,
            std::bit_cast<std::uint32_t>(1.0F / static_cast<float>(width)));
    put_u32(descriptor, 0x4CU,
            std::bit_cast<std::uint32_t>(1.0F / static_cast<float>(height)));
    put_u32(descriptor, 0x60U, 0U);
    put_u32(descriptor, 0x64U, static_cast<std::uint32_t>(dds.size()));
    put_u32(descriptor, 0x68U, 8U);

    std::vector<std::uint8_t> bundle(5U * 0x800U, 0U);
    put_u32(bundle, 0U, 4U);
    for (std::size_t slot = 0U; slot < 4U; ++slot) {
        put_u32(bundle, 4U + slot * 4U, 1U);
        const std::size_t base = (slot + 1U) * 0x800U;
        std::memcpy(bundle.data() + base, descriptor.data(), descriptor.size());
        std::memcpy(bundle.data() + base + 0x70U, dds.data(), dds.size());
    }
    return bundle;
}

dmcresource::Session make_mod_part(const char* name,
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
    session.probe.evidence = "test";
    session.probe.mime_type = "application/vnd.dmc.mod";
    session.capabilities =
        capability(ResourceCapability::Inspection) |
        ResourceCapability::Geometry |
        ResourceCapability::Wireframe |
        ResourceCapability::TextureBinding |
        ResourceCapability::UvCoordinates |
        ResourceCapability::SkeletalSkinning |
        ResourceCapability::SkinWeights;
    session.renderable = true;

    for (std::size_t triangle = 0U; triangle < texture_slots.size(); ++triangle) {
        const float triangle_x = x_offset + static_cast<float>(triangle) * 2.0F;
        const std::uint32_t base = static_cast<std::uint32_t>(session.render_mesh.vertices.size());

        Mesh local;
        local.vertices = {
            {triangle_x + 0.0F, 0.0F, 0.0F},
            {triangle_x + 1.0F, 0.0F, 0.0F},
            {triangle_x + 0.0F, 1.0F, 0.0F},
        };
        local.indices = {0U, 1U, 2U};
        local.uv0 = {{0.0F, 0.0F}, {1.0F, 0.0F}, {0.0F, 1.0F}};

        MeshPrimitive primitive;
        primitive.name = std::string{name} + " mesh " + std::to_string(triangle);
        primitive.mesh = local;
        primitive.object_index = 0U;
        primitive.mesh_index = static_cast<std::uint32_t>(triangle);
        primitive.node_index = -1;
        const auto primitive_index = static_cast<std::uint32_t>(session.scene.meshes.size());
        session.scene.meshes.push_back(std::move(primitive));
        session.scene.textures.push_back({
            primitive_index,
            texture_slots[triangle],
            std::string{name} + ".ptx",
        });

        session.render_mesh.vertices.insert(
            session.render_mesh.vertices.end(),
            local.vertices.begin(), local.vertices.end());
        session.render_mesh.uv0.insert(
            session.render_mesh.uv0.end(),
            local.uv0.begin(), local.uv0.end());
        session.render_mesh.indices.push_back(base + 0U);
        session.render_mesh.indices.push_back(base + 1U);
        session.render_mesh.indices.push_back(base + 2U);
        session.render_triangle_texture_slots.push_back(texture_slots[triangle]);
    }

    RenderNode node;
    node.name = std::string{name} + " root";
    node.kind = RenderNodeKind::Bone;
    node.parent = -1;
    node.parent_authority = true;
    node.spatial_authority = false;
    session.scene.nodes.push_back(node);

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

    // Mirrors the sparse local texture-slot usage observed in the real em028
    // four-MOD boss corpus supplied for device acceptance:
    //   em028_001 -> {1}
    //   em028_004 -> {0}
    //   em028_005 -> {2,3}
    //   em028_006 -> {2}
    // Unreferenced local PTX slots are intentionally not projected globally.
    auto body = make_mod_part("em028_001", 0.0F, {1U});
    auto core = make_mod_part("em028_004", 10.0F, {0U});
    auto wings = make_mod_part("em028_005", 20.0F, {2U, 3U});
    auto tail = make_mod_part("em028_006", 30.0F, {2U});

    std::vector<const Session*> sources{&body, &core, &wings, &tail};
    std::vector<std::string> names{
        "em028_001.mod", "em028_004.mod", "em028_005.mod", "em028_006.mod"};

    auto composite = compose_mod_sessions(sources, names);
    assert(composite != nullptr);
    assert(composite->probe.format == Format::Mod);
    assert(composite->renderable);
    assert(composite->composite_parts.size() == 4U);
    assert(session_composite_part_count(composite.get()) == 4U);
    assert(session_composite_part_name(composite.get(), 0) == "em028_001.mod");
    assert(session_composite_part_name(composite.get(), 1) == "em028_004.mod");
    assert(session_composite_part_name(composite.get(), 2) == "em028_005.mod");
    assert(session_composite_part_name(composite.get(), 3) == "em028_006.mod");

    // Required-slot namespaces are compact and non-overlapping. PTX attachment
    // decodes only required local slots (0..max_required), so sparse unused
    // texture entries cannot spill into the following CompositePart namespace.
    assert(composite->composite_parts[0].texture_slot_base == 0U);
    assert(composite->composite_parts[0].texture_slot_span == 2U);
    assert(composite->composite_parts[1].texture_slot_base == 2U);
    assert(composite->composite_parts[1].texture_slot_span == 1U);
    assert(composite->composite_parts[2].texture_slot_base == 3U);
    assert(composite->composite_parts[2].texture_slot_span == 4U);
    assert(composite->composite_parts[3].texture_slot_base == 7U);
    assert(composite->composite_parts[3].texture_slot_span == 3U);

    // The top-level scene keeps only the merged hierarchy namespace. Geometry,
    // skins and texture bindings stay source-local; one flattened mesh powers
    // the shared camera/render path.
    assert(composite->scene.meshes.empty());
    assert(composite->scene.skins.empty());
    assert(composite->scene.textures.empty());
    assert(composite->scene.nodes.size() == 4U);
    assert(composite->scene.nodes[0].name == "em028_001.mod / em028_001 root");
    assert(composite->scene.nodes[1].name == "em028_004.mod / em028_004 root");
    assert(composite->scene.nodes[2].name == "em028_005.mod / em028_005 root");
    assert(composite->scene.nodes[3].name == "em028_006.mod / em028_006 root");

    const std::vector<std::uint32_t> expected_global_slots{1U, 2U, 5U, 6U, 9U};
    assert(composite->render_triangle_texture_slots == expected_global_slots);
    assert(composite->render_mesh.vertices.size() == 15U);
    assert(composite->render_mesh.indices.size() == 15U);
    assert(composite->render_mesh.uv0.size() == 15U);
    assert(composite->render_mesh.vertices[0].x == 0.0F);
    assert(composite->render_mesh.vertices[3].x == 10.0F);
    assert(composite->render_mesh.vertices[6].x == 20.0F);
    assert(composite->render_mesh.vertices[12].x == 30.0F);

    auto state = black_widow_state(composite.get());
    assert(widow::has_state(state, widow::StateFlag::CanRender));
    assert(widow::has_state(state, widow::StateFlag::CanShowUv));
    assert(widow::has_state(state, widow::StateFlag::TextureCompanionAttachable));
    assert(widow::has_state(state, widow::StateFlag::CanAddModelPart));
    assert(widow::has_state(state, widow::StateFlag::CanStageCompanion));
    assert(!widow::has_state(state, widow::StateFlag::TextureCompanionAttached));
    assert(!widow::has_state(state, widow::StateFlag::CanExportPng));

    // A broken top-level slot projection disables merged UV actions without
    // destroying explicit per-part PTX attachability from retained source scenes.
    composite->render_triangle_texture_slots[2] =
        std::numeric_limits<std::uint32_t>::max();
    state = black_widow_state(composite.get());
    assert(!widow::has_state(state, widow::StateFlag::CanShowUv));
    assert(widow::has_state(state, widow::StateFlag::TextureCompanionAttachable));
    composite->render_triangle_texture_slots[2] = 5U;

    // One canonical four-slot PTX can be an explicit shared texture bank for all
    // four source-local MOD namespaces. The top-level renderer still consumes
    // only globally remapped slots.
    const auto shared_ptx = make_shared_four_slot_ptx();
    assert(attach_session_ptx(
        composite.get(), "em028_000.ptx", shared_ptx.data(), shared_ptx.size()));
    assert(composite->texture_companion_attached);
    for (const auto& part : composite->composite_parts) {
        assert(part.texture_companion_attached);
    }
    assert(composite->attached_textures.size() == 10U);
    for (const std::uint32_t slot : expected_global_slots) {
        assert(composite->attached_textures[slot].available());
    }
    assert(composite->texture_attachment_detail.find("Shared PTX attached")
           != std::string::npos);

    state = black_widow_state(composite.get());
    assert(widow::has_state(state, widow::StateFlag::TextureCompanionAttached));

    // A failed replacement is transactional: the already attached shared bank
    // must survive intact.
    const auto retained_slot_9 = composite->attached_textures[9].rgba8;
    assert(!attach_session_ptx(composite.get(), "bad.ptx", nullptr, 0U));
    assert(composite->texture_companion_attached);
    assert(composite->attached_textures[9].rgba8 == retained_slot_9);

    // Invalid/non-MOD mixtures are rejected rather than silently flattened.
    auto not_mod = tail;
    not_mod.probe.format = Format::Scm;
    sources[3] = &not_mod;
    assert(compose_mod_sessions(sources, names) == nullptr);

    return 0;
}
