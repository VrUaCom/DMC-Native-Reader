#include <bit>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "dmcresource/resource_capabilities.h"
#include "dmcresource/resource_session.h"
#include "dmcresource/spider/session_actions.h"

namespace {

void put_u16(std::vector<std::uint8_t>& bytes,
             std::size_t offset,
             std::uint16_t value) {
    bytes[offset] = static_cast<std::uint8_t>(value & 0xFFU);
    bytes[offset + 1U] = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
}

void put_u32(std::vector<std::uint8_t>& bytes,
             std::size_t offset,
             std::uint32_t value) {
    for (std::size_t i = 0U; i < 4U; ++i) {
        bytes[offset + i] = static_cast<std::uint8_t>((value >> (8U * i)) & 0xFFU);
    }
}

std::vector<std::uint8_t> make_one_slot_ptx() {
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

    std::vector<std::uint8_t> bundle(2U * 0x800U, 0U);
    put_u32(bundle, 0U, 1U);
    put_u32(bundle, 4U, 1U);
    std::memcpy(bundle.data() + 0x800U, descriptor.data(), descriptor.size());
    std::memcpy(bundle.data() + 0x870U, dds.data(), dds.size());
    return bundle;
}

dmcresource::Session make_part(const char* name, float x) {
    using namespace dmcresource;
    Session session;
    session.probe.format = Format::Mod;
    session.probe.recognized = true;
    session.probe.content_confirmed = true;
    session.probe.family = "MOD";
    session.probe.domain = "model";
    session.probe.support = "read";
    session.probe.evidence = "test";
    session.capabilities = capability(ResourceCapability::Geometry) |
        ResourceCapability::TextureBinding;
    session.renderable = true;

    Mesh mesh;
    mesh.vertices = {{x, 0.0F, 0.0F}, {x + 1.0F, 0.0F, 0.0F}, {x, 1.0F, 0.0F}};
    mesh.indices = {0U, 1U, 2U};
    MeshPrimitive primitive;
    primitive.name = name;
    primitive.mesh = mesh;
    session.scene.meshes.push_back(primitive);
    session.scene.textures.push_back({0U, 0U, "test.ptx"});
    session.render_mesh = mesh;
    session.render_triangle_texture_slots = {0U};
    return session;
}

void assert_same_images(const std::vector<dmcresource::ImagePreview>& a,
                        const std::vector<dmcresource::ImagePreview>& b) {
    assert(a.size() == b.size());
    for (std::size_t index = 0U; index < a.size(); ++index) {
        assert(a[index].width == b[index].width);
        assert(a[index].height == b[index].height);
        assert(a[index].rgba8 == b[index].rgba8);
    }
}

}  // namespace

int main() {
    using namespace dmcresource;
    namespace actions = dmcresource::spider::actions;

    auto body = make_part("body", 0.0F);
    auto hair = make_part("hair", 2.0F);
    std::vector<const Session*> parts{&body, &hair};
    std::vector<std::string> names{"body.mod", "hair.mod"};
    auto composite = actions::compose_mod_sessions(parts, names);
    assert(composite != nullptr);

    const auto ptx = make_one_slot_ptx();
    assert(actions::attach_ptx(composite.get(), "shared.ptx", ptx.data(), ptx.size()));
    assert(composite->attached_textures.size() == 1U);

    const auto retained_textures = composite->attached_textures;
    const auto retained_slots = composite->render_triangle_texture_slots;
    const auto retained_part_state = composite->composite_parts[1].texture_companion_attached;

    // Force a post-decode composite-range rejection. The PTX itself is valid,
    // so attach_ptx_to_part reaches the per-part transaction boundary before
    // rejecting the inconsistent flattened projection.
    composite->render_triangle_texture_slots.pop_back();
    assert(!actions::attach_ptx_to_part(
        composite.get(), 1, "replacement.ptx", ptx.data(), ptx.size()));
    assert_same_images(composite->attached_textures, retained_textures);
    assert(composite->composite_parts[1].texture_companion_attached == retained_part_state);

    // Restore the deliberately damaged projection and prove a normal
    // replacement still succeeds.
    composite->render_triangle_texture_slots = retained_slots;
    assert(actions::attach_ptx_to_part(
        composite.get(), 1, "replacement.ptx", ptx.data(), ptx.size()));
    assert(composite->attached_textures.size() == 1U);
    assert(composite->render_triangle_texture_slots == retained_slots);

    return 0;
}
