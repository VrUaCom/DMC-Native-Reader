#include "dmcresource/motion/motion_player.h"
#include "dmcresource/motion/cloth_chain.h"
#include "dmcresource/motion/motion_script.h"
#include "dmcresource/motion/part_attachment.h"
#include "dmcresource/motion/uv_scroll.h"
#include "dmcresource/pac_assembly.h"
#include "dmcresource/resource_session.h"
#include "dmcresource/shadow_hull.h"

#include <bit>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

// IPlayer coat regression (EXE: CPlVergil 0x140225A16 / 0x1402260A4,
// CPlDante 0x1402120A7): in a player PAC the coat MOD (slot 12) uses the body
// texture (slot 0) and its skeleton hangs from body joint 3 with an identity
// root local, following the animated body every frame.
namespace {

void put_u8(std::vector<std::uint8_t>& bytes,
            std::size_t offset,
            std::uint8_t value) {
    assert(offset < bytes.size());
    bytes[offset] = value;
}

void put_u16(std::vector<std::uint8_t>& bytes,
             std::size_t offset,
             std::uint16_t value) {
    assert(offset + 2U <= bytes.size());
    put_u8(bytes, offset + 0U,
           static_cast<std::uint8_t>(value & 0xFFU));
    put_u8(bytes, offset + 1U,
           static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
}

void put_u32(std::vector<std::uint8_t>& bytes,
             std::size_t offset,
             std::uint32_t value) {
    assert(offset + 4U <= bytes.size());
    for (std::size_t i = 0U; i < 4U; ++i) {
        put_u8(bytes, offset + i,
               static_cast<std::uint8_t>(
                   (value >> (i * 8U)) & 0xFFU));
    }
}

void put_u64(std::vector<std::uint8_t>& bytes,
             std::size_t offset,
             std::uint64_t value) {
    assert(offset + 8U <= bytes.size());
    for (std::size_t i = 0U; i < 8U; ++i) {
        put_u8(bytes, offset + i,
               static_cast<std::uint8_t>(
                   (value >> (i * 8U)) & 0xFFU));
    }
}

void put_f32(std::vector<std::uint8_t>& bytes,
             std::size_t offset,
             float value) {
    put_u32(bytes, offset, std::bit_cast<std::uint32_t>(value));
}

void put_transform(std::vector<std::uint8_t>& bytes,
                   std::size_t offset,
                   float tx,
                   float ty,
                   float tz,
                   float magnitude) {
    put_f32(bytes, offset + 0x00U, tx);
    put_f32(bytes, offset + 0x04U, ty);
    put_f32(bytes, offset + 0x08U, tz);
    put_f32(bytes, offset + 0x0CU, magnitude);
    put_f32(bytes, offset + 0x10U, 0.0F);
    put_f32(bytes, offset + 0x14U, 0.0F);
    put_f32(bytes, offset + 0x18U, 0.0F);
    put_f32(bytes, offset + 0x1CU, 0.0F);
}

std::vector<std::uint8_t> make_spatial_mod() {
    std::vector<std::uint8_t> bytes(0x2A0U, 0U);
    bytes[0] = 'M';
    bytes[1] = 'O';
    bytes[2] = 'D';
    bytes[3] = ' ';
    put_f32(bytes, 0x04U, 1.01F);
    put_u8(bytes, 0x10U, 1U);
    put_u8(bytes, 0x11U, 3U);
    put_u64(bytes, 0x20U, 0x200U);

    // One outer model, one 3-vertex inner mesh.
    put_u8(bytes, 0x40U, 1U);
    put_u16(bytes, 0x42U, 3U);
    put_u64(bytes, 0x48U, 0x80U);

    put_u16(bytes, 0x80U, 3U);
    put_u16(bytes, 0x82U, 9U);
    put_u16(bytes, 0x84U, 1U);
    put_u16(bytes, 0x86U, 2U);
    put_u16(bytes, 0x88U, 3U);
    put_u16(bytes, 0x8AU, 4U);
    put_u64(bytes, 0x90U, 0xD0U);
    put_u64(bytes, 0x98U, 0x100U);
    put_u64(bytes, 0xA0U, 0x130U);
    put_u64(bytes, 0xA8U, 0x140U);
    put_u64(bytes, 0xB0U, 0x150U);
    put_u64(bytes, 0xB8U, 0U);
    put_u64(bytes, 0xC0U, 0xE0U);
    put_u32(bytes, 0xC8U, 0U);
    put_u32(bytes, 0xCCU, 0U);

    put_f32(bytes, 0xD0U, 0.0F);
    put_f32(bytes, 0xD4U, 0.0F);
    put_f32(bytes, 0xD8U, 0.0F);
    put_f32(bytes, 0xDCU, 1.0F);
    put_f32(bytes, 0xE0U, 0.0F);
    put_f32(bytes, 0xE4U, 0.0F);
    put_f32(bytes, 0xE8U, 0.0F);
    put_f32(bytes, 0xECU, 1.0F);
    put_f32(bytes, 0xF0U, 0.0F);

    for (std::size_t i = 0U; i < 3U; ++i) {
        const auto n = 0x100U + i * 12U;
        put_f32(bytes, n + 0U, 0.0F);
        put_f32(bytes, n + 4U, 0.0F);
        put_f32(bytes, n + 8U, 1.0F);
    }

    put_u16(bytes, 0x130U, 0U);
    put_u16(bytes, 0x132U, 0U);
    put_u16(bytes, 0x134U, 4096U);
    put_u16(bytes, 0x136U, 0U);
    put_u16(bytes, 0x138U, 0U);
    put_u16(bytes, 0x13AU, 4096U);

    // BLENDINDICES remain zero. Control values are a valid one-influence skin
    // encoding for bone 0 while the transform domain contains three nodes.
    put_u16(bytes, 0x150U, 0x001FU);
    put_u16(bytes, 0x152U, 0x001FU);
    put_u16(bytes, 0x154U, 0x001FU);

    // Canonical node-domain shell for count=3:
    // parent +0x20, order +0x24, adapter +0x28, transforms +0x30.
    put_u32(bytes, 0x200U, 0x20U);
    put_u32(bytes, 0x204U, 0x24U);
    put_u32(bytes, 0x208U, 0x28U);
    put_u32(bytes, 0x20CU, 0x30U);

    // Non-linear evaluation order proves parent values are node indices:
    // root node0 -> node2 -> node1.
    put_u8(bytes, 0x220U, 0xFFU);
    put_u8(bytes, 0x221U, 0U);
    put_u8(bytes, 0x222U, 2U);

    put_u8(bytes, 0x224U, 0U);
    put_u8(bytes, 0x225U, 2U);
    put_u8(bytes, 0x226U, 1U);

    put_u8(bytes, 0x228U, 0U);
    put_u8(bytes, 0x229U, 0U);
    put_u8(bytes, 0x22AU, 0U);

    put_transform(bytes, 0x230U, 10.0F, 0.0F, 0.0F, 10.0F);
    put_transform(bytes, 0x250U, 0.0F, 0.0F, 2.0F, 2.0F);
    put_transform(bytes, 0x270U, 0.0F, 5.0F, 0.0F, 5.0F);
    return bytes;
}

// Three-node MOT: node 0 translation-x only, one compression-2 track with
// keys (frame 0 -> 10.0) and (frame 10 -> 20.0).
std::vector<std::uint8_t> make_translation_mot() {
    std::vector<std::uint8_t> bytes(0x50U, 0U);
    put_u32(bytes, 0x00U, 0x30U);
    bytes[4] = 'M';
    bytes[5] = 'O';
    bytes[6] = 'T';
    bytes[7] = 0;
    put_f32(bytes, 0x0CU, 10.0F);
    put_f32(bytes, 0x14U, 10.0F);
    put_u16(bytes, 0x1CU, 3U);
    put_u16(bytes, 0x1EU, 0x040U);
    put_u32(bytes, 0x30U, 1U);
    put_u16(bytes, 0x34U, 0x18U);
    put_u16(bytes, 0x36U, 2U);
    put_u16(bytes, 0x38U, 2U);
    put_u16(bytes, 0x3AU, 0U);
    put_f32(bytes, 0x3CU, 10.0F);
    put_f32(bytes, 0x40U, 10.0F);
    put_u16(bytes, 0x44U, 0U);
    put_u16(bytes, 0x46U, 0U);
    put_u16(bytes, 0x48U, 10U);
    put_u16(bytes, 0x4AU, 0xFFFFU);
    return bytes;
}

std::vector<std::uint8_t> make_pac(const std::vector<std::vector<std::uint8_t>>& payloads) {
    std::size_t cursor = 8U + payloads.size() * 4U;
    cursor = (cursor + 0x0FU) & ~std::size_t{0x0FU};
    std::vector<std::uint8_t> bytes(cursor, 0U);
    bytes[0] = 'P';
    bytes[1] = 'A';
    bytes[2] = 'C';
    bytes[3] = 0U;
    put_u32(bytes, 4U, static_cast<std::uint32_t>(payloads.size()));
    for (std::size_t index = 0U; index < payloads.size(); ++index) {
        put_u32(bytes, 8U + index * 4U, static_cast<std::uint32_t>(bytes.size()));
        bytes.insert(bytes.end(), payloads[index].begin(), payloads[index].end());
        bytes.resize((bytes.size() + 0x0FU) & ~std::size_t{0x0FU}, 0U);
    }
    return bytes;
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

// Four-node chain body: node k sits 1.0 above node k-1, so node 3 is at y=3.
std::vector<std::uint8_t> make_body_mod() {
    auto bytes = make_spatial_mod();
    bytes.resize(0x2C0U, 0U);
    put_u8(bytes, 0x11U, 4U);
    for (std::size_t i = 0x220U; i < 0x2C0U; ++i) bytes[i] = 0U;
    const std::uint8_t parents[4] = {0xFFU, 0U, 1U, 2U};
    for (std::size_t n = 0U; n < 4U; ++n) {
        put_u8(bytes, 0x220U + n, parents[n]);
        put_u8(bytes, 0x224U + n, static_cast<std::uint8_t>(n));
        put_u8(bytes, 0x228U + n, 0U);
    }
    put_transform(bytes, 0x230U, 0.0F, 0.0F, 0.0F, 0.0F);
    put_transform(bytes, 0x250U, 0.0F, 1.0F, 0.0F, 1.0F);
    put_transform(bytes, 0x270U, 0.0F, 1.0F, 0.0F, 1.0F);
    put_transform(bytes, 0x290U, 0.0F, 1.0F, 0.0F, 1.0F);
    return bytes;
}

// `count`-node chain body: node n hangs from n-1, one unit up (y = n).
std::vector<std::uint8_t> make_chain_body(std::size_t count) {
    auto bytes = make_spatial_mod();
    const std::size_t parents = 0x220U;
    const std::size_t order = parents + count;
    const std::size_t adapter = order + count;
    const std::size_t transforms = (adapter + count + 0x0FU) & ~std::size_t{0x0FU};
    bytes.resize(transforms + count * 0x20U, 0U);
    for (std::size_t i = 0x200U; i < bytes.size(); ++i) bytes[i] = 0U;
    put_u8(bytes, 0x11U, static_cast<std::uint8_t>(count));
    put_u32(bytes, 0x200U, static_cast<std::uint32_t>(parents - 0x200U));
    put_u32(bytes, 0x204U, static_cast<std::uint32_t>(order - 0x200U));
    put_u32(bytes, 0x208U, static_cast<std::uint32_t>(adapter - 0x200U));
    put_u32(bytes, 0x20CU, static_cast<std::uint32_t>(transforms - 0x200U));
    for (std::size_t n = 0U; n < count; ++n) {
        put_u8(bytes, parents + n, n == 0U ? 0xFFU : static_cast<std::uint8_t>(n - 1U));
        put_u8(bytes, order + n, static_cast<std::uint8_t>(n));
        const float y = n == 0U ? 0.0F : 1.0F;
        put_transform(bytes, transforms + n * 0x20U, 0.0F, y, 0.0F, y);
    }
    return bytes;
}

// One closed SHW hull (tetrahedron, T = 2V - 4) whose four vertices all
// follow `joint`; header +0x11 names the model node count.
std::vector<std::uint8_t> make_tetra_shw(std::uint8_t node_count, std::uint8_t joint) {
    std::vector<std::uint8_t> bytes(0x200U, 0U);
    bytes[0] = 'S';
    bytes[1] = 'H';
    bytes[2] = 'W';
    bytes[3] = ' ';
    put_f32(bytes, 0x04U, 0.5F);
    put_u8(bytes, 0x10U, 1U);
    put_u8(bytes, 0x11U, node_count);
    const std::size_t record = 0x20U;
    put_u16(bytes, record + 0x00U, 4U);
    put_u16(bytes, record + 0x02U, 4U);
    put_u64(bytes, record + 0x10U, 0x60U);   // triangles
    put_u64(bytes, record + 0x18U, 0xA0U);   // adjacency
    put_u64(bytes, record + 0x20U, 0xC0U);   // vertices
    put_u64(bytes, record + 0x28U, 0x100U);  // selectors
    const std::uint32_t triangles[4][3] = {{0, 1, 2}, {0, 3, 1}, {0, 2, 3}, {1, 3, 2}};
    for (std::size_t t = 0U; t < 4U; ++t) {
        for (std::size_t k = 0U; k < 3U; ++k) {
            put_u32(bytes, 0x60U + t * 0x10U + k * 4U, triangles[t][k]);
        }
        std::size_t lane = 0U;
        for (std::size_t other = 0U; other < 4U; ++other) {
            if (other == t) continue;
            put_u16(bytes, 0xA0U + t * 8U + lane * 2U, static_cast<std::uint16_t>(other));
            ++lane;
        }
    }
    const float vertices[4][3] = {{0.0F, 3.0F, 0.0F}, {1.0F, 3.0F, 0.0F},
                                  {0.0F, 4.0F, 0.0F}, {0.0F, 3.0F, 1.0F}};
    for (std::size_t v = 0U; v < 4U; ++v) {
        put_f32(bytes, 0xC0U + v * 0x10U + 0x0U, vertices[v][0]);
        put_f32(bytes, 0xC0U + v * 0x10U + 0x4U, vertices[v][1]);
        put_f32(bytes, 0xC0U + v * 0x10U + 0x8U, vertices[v][2]);
        put_f32(bytes, 0xC0U + v * 0x10U + 0xCU, 1.0F);
        put_u8(bytes, 0x100U + v, joint);
    }
    return bytes;
}

// Four-node MOT: node 0 translation-x from 0 (frame 0) to 10 (frame 10).
std::vector<std::uint8_t> make_body_mot() {
    auto bytes = make_translation_mot();
    put_u16(bytes, 0x1CU, 4U);
    put_u16(bytes, 0x1EU, 0x040U);
    put_u16(bytes, 0x20U, 0U);
    put_u16(bytes, 0x22U, 0U);
    put_u16(bytes, 0x24U, 0U);
    put_f32(bytes, 0x3CU, 0.0F);
    put_f32(bytes, 0x40U, 10.0F);
    return bytes;
}

[[nodiscard]] bool near(float a, float b) { return std::fabs(a - b) < 0.0005F; }

}  // namespace

int main() {
    namespace motion = dmcresource::motion;
    const auto ptx = make_one_slot_ptx();
    const auto body = make_body_mod();
    const auto coat = make_spatial_mod();   // 3 nodes, root rest at x=10
    const auto mot = make_body_mot();
    const std::vector<std::uint8_t> filler(16U, 0xABU);

    std::vector<std::vector<std::uint8_t>> slots(14U, filler);
    slots[0] = ptx;
    slots[1] = body;
    slots[12] = coat;
    slots[13] = mot;
    const auto pac = make_pac(slots);

    auto archive = dmcresource::open_session("pl001.pac", pac.data(), pac.size());
    assert(archive != nullptr);
    dmcresource::pac_assembly::AssemblyReport report;
    auto scene = dmcresource::pac_assembly::assemble_pac(*archive, &report, "pl001.pac");
    assert(scene != nullptr);
    assert(report.models == 2U);
    assert(report.attached_parts == 1U);
    assert(report.textures_attached == 2U);       // slot 0 shared by body and coat
    assert(scene->texture_companion_attached);
    assert(scene->composite_parts.size() == 2U);
    assert(motion::is_attached_part(scene.get(), 1U));
    assert(scene->composite_parts[1].placement.attachment_selector == 3U);

    // Coat vertex = rest - coatRestRoot(10,0,0) + bodyJoint3(0,3,0).
    const auto& coat_rest = scene->composite_parts[1].scene.meshes[0].mesh.vertices;
    const std::size_t coat_begin = scene->composite_parts[0].scene.meshes[0].mesh.vertices.size();
    for (std::size_t i = 0U; i < coat_rest.size(); ++i) {
        const auto& v = scene->render_mesh.vertices[coat_begin + i];
        assert(near(v.x, coat_rest[i].x - 10.0F));
        assert(near(v.y, coat_rest[i].y + 3.0F));
        assert(near(v.z, coat_rest[i].z));
    }

    // Playing the body MOT moves joint 3, and the coat follows it.
    assert(scene->motion_library.size() == 1U);
    const auto& payload = scene->motion_library.front();
    const auto loaded = motion::load_motion(
        scene.get(), payload.name, payload.bytes.data(), payload.bytes.size());
    assert(loaded.ok && loaded.animated_parts == 1U);
    assert(motion::apply_motion_frame(scene.get(), 5.0F));
    for (std::size_t i = 0U; i < coat_rest.size(); ++i) {
        const auto& v = scene->render_mesh.vertices[coat_begin + i];
        assert(near(v.x, coat_rest[i].x - 10.0F + 5.0F));
        assert(near(v.y, coat_rest[i].y + 3.0F));
    }

    // SHW: slot 8 pairs with the 4-node body through header +0x11 and its
    // hull follows joint 3 (skin matrix), also while the MOT plays.
    {
        std::vector<std::vector<std::uint8_t>> shadow_slots = slots;
        shadow_slots[8] = make_tetra_shw(4U, 3U);
        const auto shadow_pac = make_pac(shadow_slots);
        auto shadow_archive = dmcresource::open_session("pl001.pac", shadow_pac.data(),
                                                        shadow_pac.size());
        assert(shadow_archive != nullptr);
        auto hull_view = dmcresource::open_session(
            "slot_0008.shw", shadow_slots[8].data(), shadow_slots[8].size());
        assert(hull_view != nullptr && hull_view->renderable);
        assert(hull_view->render_mesh.vertices.size() == 4U);
        dmcresource::pac_assembly::AssemblyReport shadow_report;
        auto shaded = dmcresource::pac_assembly::assemble_pac(*shadow_archive, &shadow_report,
                                                              "pl001.pac");
        assert(shaded != nullptr && shadow_report.shadows_bound == 1U);
        assert(shaded->shadow_bindings.size() == 1U);
        assert(shaded->shadow_bindings[0].node_count == 4U);
        const auto rest = dmcresource::shadow::posed_hull_triangles(*shaded);
        assert(rest.size() == 12U);
        assert(near(rest[0].x, 0.0F) && near(rest[0].y, 3.0F));
        const auto floor = dmcresource::shadow::floor_shadow_triangles(
            *shaded, {0.0F, -1.0F, 0.0F}, 0.0F);
        assert(floor.size() == 12U && near(floor[0].y, 0.0F) && near(floor[0].x, 0.0F));

        const auto& clip = shaded->motion_library.front();
        const auto bound = motion::load_motion(shaded.get(), clip.name, clip.bytes.data(),
                                               clip.bytes.size());
        assert(bound.ok);
        assert(motion::apply_motion_frame(shaded.get(), 5.0F));
        const auto moved = dmcresource::shadow::posed_hull_triangles(*shaded);
        assert(near(moved[0].x, 5.0F) && near(moved[0].y, 3.0F));
        // A mismatched node count is never bound.
        shadow_slots[8] = make_tetra_shw(7U, 3U);
        const auto odd_pac = make_pac(shadow_slots);
        auto odd = dmcresource::open_session("pl001.pac", odd_pac.data(), odd_pac.size());
        dmcresource::pac_assembly::AssemblyReport odd_report;
        auto unbound = dmcresource::pac_assembly::assemble_pac(*odd, &odd_report, "pl001.pac");
        assert(unbound != nullptr && odd_report.shadows_bound == 0U);
    }

    // Rebellion: plwp_sword.pac added to the character hangs from body joint 3
    // at local(T(-14.5,32,-14), R(-1.658,0,3.403)) x joint3World.
    {
        std::vector<std::vector<std::uint8_t>> weapon_slots{ptx, coat};
        const auto weapon_pac = make_pac(weapon_slots);
        auto weapon = dmcresource::open_session("plwp_sword.pac", weapon_pac.data(),
                                                weapon_pac.size());
        assert(weapon != nullptr);
        const dmcresource::Session* archives[] = {archive.get(), weapon.get()};
        const std::string_view names[] = {"pl000.pac", "plwp_sword.pac"};
        auto armed = dmcresource::pac_assembly::assemble_archives(archives, names, &report);
        assert(armed != nullptr);
        assert(report.models == 3U);
        assert(report.attached_parts == 2U);                 // coat + Rebellion
        assert(motion::is_attached_part(armed.get(), 2U));
        const auto& placement = armed->composite_parts[2].placement;
        assert(placement.attachment_selector == 3U);
        const auto record = motion::weapon_record_for_archive("obj\\PLWP_SWORD.PAC");
        assert(record.has_value() && record->class_name == "CPlWpSword");
        const auto offset = motion::weapon_offset_matrix(*record);
        assert(placement.attachment_offset.values == offset.values);
        assert(offset.values[12] == -14.5F && offset.values[13] == 32.0F &&
               offset.values[14] == -14.0F);
        // Weapon root node world = restLocal(root) x offset x joint3.
        const auto body_nodes = armed->composite_parts[0].scene.nodes.size();
        const auto coat_nodes = armed->composite_parts[1].scene.nodes.size();
        const auto& joint3 = armed->scene.nodes[3].world.values;
        const auto& weapon_root = armed->scene.nodes[body_nodes + coat_nodes].world.values;
        // Root local is a pure translation (10,0,0) in the fixture.
        const float ox = 10.0F * offset.values[0] + offset.values[12];
        const float oy = 10.0F * offset.values[1] + offset.values[13];
        assert(near(weapon_root[12], ox * joint3[0] + oy * joint3[4] + joint3[12]));
        assert(near(weapon_root[13], ox * joint3[1] + oy * joint3[5] + joint3[13]));
        assert(!motion::weapon_record_for_archive("plwp_gun.pac").has_value());
    }

    // Agni & Rudra: one 3-node MOD, node 2 follows part 0 and node 1 follows
    // part 1 of the state-0 record (0x1401FDA80, pose 0x140227CF0).
    {
        std::vector<std::vector<std::uint8_t>> pair_slots{ptx, coat};  // coat = 3 nodes
        const auto pair_pac = make_pac(pair_slots);
        auto pair = dmcresource::open_session("plwp_2sword.pac", pair_pac.data(),
                                              pair_pac.size());
        assert(pair != nullptr);
        const dmcresource::Session* pair_archives[] = {archive.get(), pair.get()};
        const std::string_view pair_names[] = {"pl001.pac", "plwp_2sword.pac"};
        dmcresource::pac_assembly::AssemblyReport pair_report;
        auto dual = dmcresource::pac_assembly::assemble_archives(pair_archives, pair_names,
                                                                 &pair_report);
        assert(dual != nullptr && pair_report.attached_parts == 2U);
        const auto record = motion::weapon_record_for_archive("plwp_2sword.pac");
        const auto second = motion::weapon_second_part("CPlWp2Sword");
        assert(record && second && second->first_node == 2U && second->second_node == 1U);
        const auto first_offset = motion::weapon_offset_matrix(*record);
        const auto second_offset =
            motion::attach_local_matrix(second->translation, second->rotation_xyz_radians);
        const std::size_t base = dual->composite_parts[0].scene.nodes.size() +
                                 dual->composite_parts[1].scene.nodes.size();
        const auto& joint3 = dual->scene.nodes[3].world.values;
        const auto expect = [&](const dmcresource::Matrix4& offset, std::size_t node) {
            const auto& w = dual->scene.nodes[base + node].world.values;
            for (std::size_t r = 0U; r < 4U; ++r) {
                for (std::size_t c = 0U; c < 4U; ++c) {
                    float v = 0.0F;
                    for (std::size_t k = 0U; k < 4U; ++k) {
                        v += offset.values[r * 4U + k] * joint3[k * 4U + c];
                    }
                    assert(near(w[r * 4U + c], v));
                }
            }
        };
        expect(first_offset, 2U);
        expect(second_offset, 1U);
        // The blades differ, so they no longer overlap.
        assert(!near(dual->scene.nodes[base + 1U].world.values[12],
                     dual->scene.nodes[base + 2U].world.values[12]));
        assert(!motion::weapon_second_part("CPlWpSword").has_value());
    }

    // Weapon motion banks: pl000_00_5.pac holds Agni & Rudra's motions
    // (0x14058ABC8[2 * 4] = 5); added banks are labelled in the library.
    {
        const auto bank = motion::weapon_motion_bank("motion\\pl000\\PL000_00_5.PAC");
        assert(bank && bank->weapon_id == 2U && bank->class_name == "CPlWp2Sword");
        assert(motion::weapon_motion_bank("pl000_00_3.pac")->weapon_name == "Rebellion");
        assert(!motion::weapon_motion_bank("pl000_00_0.pac").has_value());
        assert(!motion::weapon_motion_bank("pl001_00_5.pac").has_value());
        std::vector<std::vector<std::uint8_t>> bank_slots{mot};
        const auto bank_pac = make_pac(bank_slots);
        auto bank_archive = dmcresource::open_session("pl000_00_5.pac", bank_pac.data(),
                                                      bank_pac.size());
        assert(bank_archive != nullptr);
        const dmcresource::Session* bank_archives[] = {archive.get(), bank_archive.get()};
        const std::string_view bank_names[] = {"pl001.pac", "pl000_00_5.pac"};
        dmcresource::pac_assembly::AssemblyReport bank_report;
        auto with_bank = dmcresource::pac_assembly::assemble_archives(bank_archives, bank_names,
                                                                      &bank_report);
        assert(with_bank != nullptr && with_bank->motion_library.size() == 2U);
        assert(with_bank->motion_library[1].name.starts_with("Agni & Rudra · "));
        assert(!with_bank->motion_library[0].name.starts_with("Agni"));
    }

    // em000.pac feeds CEm000-CEm004: each class keeps only its body, cloth and
    // weapon slots; the weapon hangs from body joint 9 at the ZYX offset.
    {
        const auto variants = motion::enemy_variants_for("st\\EM000.PAC");
        assert(variants.size() == 6U && variants[5].class_name == "CEm005Shl01");
        assert(variants[2].class_name == "CEm002" && variants[2].cloth_count == 2U);
        assert(variants[4].weapon_slot == 34U && variants[4].cloth_count == 0U);
        assert(motion::enemy_variants_for("em001.pac").empty());
        // Single-axis offsets agree with the XYZ builder; mixed axes differ.
        const auto zyx = motion::attach_local_matrix_zyx({1.0F, 2.0F, 3.0F}, {0.3F, 0.0F, 0.0F});
        const auto xyz = motion::attach_local_matrix({1.0F, 2.0F, 3.0F}, {0.3F, 0.0F, 0.0F});
        for (std::size_t e = 0U; e < 16U; ++e) assert(near(zyx.values[e], xyz.values[e]));
        const auto mixed_zyx = motion::attach_local_matrix_zyx({0.0F, 0.0F, 0.0F}, {0.4F, 0.0F, 0.7F});
        const auto mixed_xyz = motion::attach_local_matrix({0.0F, 0.0F, 0.0F}, {0.4F, 0.0F, 0.7F});
        assert(!near(mixed_zyx.values[1], mixed_xyz.values[1]) ||
               !near(mixed_zyx.values[2], mixed_xyz.values[2]));
        // Rz x Rx for row vectors: row 0 = (cz, sz*cx, sz*sx).
        assert(near(mixed_zyx.values[0], std::cos(0.7F)));
        assert(near(mixed_zyx.values[1], std::sin(0.7F) * std::cos(0.4F)));
        assert(near(mixed_zyx.values[2], std::sin(0.7F) * std::sin(0.4F)));

        // Synthetic em000: slot 1 body (16 nodes), slot 3 cloth, slot 26 weapon,
        // slot 5 another class's body (skipped for CEm000).
        std::vector<std::vector<std::uint8_t>> em_slots(27U, filler);
        em_slots[0] = ptx;
        em_slots[1] = make_chain_body(16U);
        em_slots[3] = coat;
        em_slots[5] = make_chain_body(16U);
        em_slots[25] = ptx;
        em_slots[26] = coat;
        const auto em_pac = make_pac(em_slots);
        auto em_archive = dmcresource::open_session("em000.pac", em_pac.data(), em_pac.size());
        assert(em_archive != nullptr);
        dmcresource::pac_assembly::AssemblyReport em_report;
        auto pride = dmcresource::pac_assembly::assemble_pac(*em_archive, &em_report, "em000.pac", 0U);
        assert(pride != nullptr && em_report.models == 3U && em_report.attached_parts == 2U);
        assert(em_report.enemy_class == "CEm000 A" && em_report.variant_models_skipped == 1U);
        const auto weapon_offset = motion::attach_local_matrix_zyx(variants[0].weapon_translation,
                                                                   variants[0].weapon_rotation_zyx);
        const std::size_t weapon_node = pride->composite_parts[0].scene.nodes.size() +
                                        pride->composite_parts[1].scene.nodes.size();
        const auto& joint9 = pride->scene.nodes[9].world.values;
        const auto& weapon_root = pride->scene.nodes[weapon_node].world.values;
        // Weapon root = local(rest) x offset x joint9; the synthetic root rest is x=10.
        float expected_x = 0.0F;
        const float root_rest[4] = {10.0F, 0.0F, 0.0F, 1.0F};
        for (std::size_t k = 0U; k < 4U; ++k) {
            float row = 0.0F;
            for (std::size_t j = 0U; j < 4U; ++j) row += root_rest[j] * weapon_offset.values[j * 4U + k];
            expected_x += row * joint9[k * 4U + 0U];
        }
        assert(near(weapon_root[12], expected_x));
        // Position 1: CEm000 with its variant 2-3 weapon (slot 29, absent here)
        // and no cloth (0x140097980 draws it for variants 0-1 only).
        auto pride_b = dmcresource::pac_assembly::assemble_pac(*em_archive, &em_report, "em000.pac", 1U);
        assert(pride_b != nullptr && em_report.enemy_class == "CEm000 B");
        assert(em_report.models == 1U && em_report.attached_parts == 0U);
        auto lust = dmcresource::pac_assembly::assemble_pac(*em_archive, &em_report, "em000.pac", 2U);
        assert(lust != nullptr && em_report.enemy_class == "CEm001 A");
        const auto positions = motion::archive_variants("em000.pac");
        assert(positions.size() == 10U && positions[8].label == "CEm004");
        assert(positions.back().label == "CEm005Shl01" && positions.back().enemy->body_slot == 23U &&
               positions.back().enemy->texture_slot == 32U);
        assert(motion::tsc_slot_for("em000.pac", 23U) == 24U);
        const auto nevan_positions = motion::archive_variants("EM028.pac");
        assert(nevan_positions.size() == 2U && nevan_positions[0].hide_slot == 5U);
        assert(nevan_positions[0].hide_count == 2U && nevan_positions[1].hide_count == 0U);
        assert(motion::archive_variants("pl000.pac").empty());
    }

    // Euler order of 0x140330450: Rx x Ry x Rz for row vectors (X first). The
    // Rebellion record combines X and Z, so the older Rz x Ry x Rx expansion
    // pointed the blade up instead of down along the back.
    {
        const auto record = motion::weapon_record_for_archive("plwp_sword.pac");
        assert(record.has_value());
        const auto m = motion::weapon_offset_matrix(*record);
        const float ax = record->rotation_xyz_radians[0];
        const float az = record->rotation_xyz_radians[2];
        const float rx[9] = {1, 0, 0, 0, std::cos(ax), std::sin(ax), 0, -std::sin(ax), std::cos(ax)};
        const float rz[9] = {std::cos(az), std::sin(az), 0, -std::sin(az), std::cos(az), 0, 0, 0, 1};
        for (int r = 0; r < 3; ++r) {
            for (int c = 0; c < 3; ++c) {
                float expected = 0.0F;  // Ry is identity (y = 0)
                for (int k = 0; k < 3; ++k) {
                    expected += rx[r * 3 + k] * rz[k * 3 + c];
                }
                assert(near(m.values[static_cast<std::size_t>(r * 4 + c)], expected));
            }
        }
    }

    // Nevan (em028): slot 4 nodes 0, 1, 2 follow body joints 3, 4, 5
    // (CEm028 init 0x140130480, constraint mode 1 with identity offset).
    {
        std::vector<std::vector<std::uint8_t>> enemy_slots(10U, filler);
        enemy_slots[0] = ptx;
        enemy_slots[1] = make_chain_body(16U);
        enemy_slots[4] = coat;  // 3-node part, root rest at x=10
        const auto enemy_pac = make_pac(enemy_slots);
        auto enemy_archive = dmcresource::open_session("em028.pac", enemy_pac.data(),
                                                       enemy_pac.size());
        assert(enemy_archive != nullptr);
        dmcresource::pac_assembly::AssemblyReport enemy_report;
        auto nevan = dmcresource::pac_assembly::assemble_pac(*enemy_archive, &enemy_report,
                                                             "st\\EM028.PAC");
        assert(nevan != nullptr);
        assert(enemy_report.models == 2U && enemy_report.attached_parts == 1U);
        assert(motion::is_attached_part(nevan.get(), 1U));
        const auto body_nodes = nevan->composite_parts[0].scene.nodes.size();
        assert(body_nodes == 16U);
        for (std::uint32_t k = 0U; k < 3U; ++k) {
            const auto& part_world = nevan->scene.nodes[body_nodes + k].world.values;
            const auto& joint_world = nevan->scene.nodes[3U + k].world.values;
            for (std::size_t e = 0U; e < 16U; ++e) assert(near(part_world[e], joint_world[e]));
        }
        assert(near(nevan->scene.nodes[body_nodes + 2U].world.values[13], 5.0F));
        assert(motion::enemy_constraints_for("em028.pac", 5U).has_value());
        assert(!motion::enemy_constraints_for("em028.pac", 7U).has_value());
        assert(!motion::enemy_constraints_for("em029.pac", 4U).has_value());
    }

    // CLT text (";pl000_02.clt" layout) and one chain solver node (0x1402C9450).
    {
        constexpr std::string_view clt =
            ";test.clt\n\nClothNum\t1\n\nClothNo     0\nClothId     0\n"
            "Gravity     0.500000  0.000000  0.000000\nSpringForce 0.020000\n"
            "MaxSpeed    50.000000\nStiffness   0.000000\n"
            "Wind        0.000000  0.000000  0.000000\nWindLocal   1\nWindParent  0\n"
            "WindType    1\nBone      1    Y\nBone      2    NZ\nEnd\n$\n";
        const auto blocks = motion::parse_clt(clt);
        assert(blocks.size() == 1U);
        const auto& params = blocks.front();
        assert(near(params.gravity[0], 0.5F) && near(params.stiffness, 0.0F));
        assert(near(params.spring_force, 0.02F) && params.wind_local && params.limit_length);
        assert(near(params.damping, 0.99F) && near(params.floor_level, -1000000.0F));
        assert(params.bones.size() == 2U && params.bones[0].node == 1U &&
               params.bones[0].axis == 1U && params.bones[1].axis == 5U);
        assert(motion::parse_clt("ClothNo 0\n").empty());

        motion::ClothState state;
        state.params = params;
        state.sim.assign(2U, {});
        state.velocity.assign(2U, {});
        state.axis_by_node = {-1, 1};
        std::array<float, 16> parent{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
        auto target = parent;
        target[13] = -10.0F;  // rest: 10 below the parent, +Y points at it
        state.sim[1] = target;
        std::array<float, 16> world{};
        for (int frame = 0; frame < 300; ++frame) {
            world = motion::step_cloth_node(state, 1U, target, parent, parent, 10.0F, 1.0F);
        }
        const float length = std::sqrt(world[12] * world[12] + world[13] * world[13] +
                                       world[14] * world[14]);
        assert(near(length, 10.0F));        // LimitLength keeps the bone length
        assert(world[12] > 5.0F);           // sideways gravity swung it out
        const float up = (0.0F - world[12]) * world[4] + (0.0F - world[13]) * world[5] -
                         world[14] * world[6];
        assert(near(up, 10.0F));            // Y axis still points at the parent
        // Capsule push-out (0x1402D0630): a node inside a capsule lands on its
        // surface and loses its x/z velocity.
        {
            motion::ClothState hit_state = state;
            hit_state.axis_by_node[1] = 1;
            hit_state.sim[1] = target;
            hit_state.velocity[1] = {1.0F, 0.0F, 1.0F};
            const std::array<motion::WorldCapsule, 1> capsule{{
                {{3.0F, -20.0F, 0.0F}, {3.0F, 0.0F, 0.0F}, 5.0F}}};
            const auto pushed = motion::step_cloth_node(hit_state, 1U, target, parent, parent,
                                                        10.0F, 1.0F, capsule);
            const float dx = pushed[12] - 3.0F;
            const float dz = pushed[14];
            assert(std::sqrt(dx * dx + dz * dz) > 4.0F);   // outside the core
            assert(std::fabs(hit_state.velocity[1][2]) < 1.0F);
            assert(motion::kPlayerCoatCapsules.size() == 6U &&
                   motion::kPlayerCoatCapsules[0].host_joint == 3U &&
                   near(motion::kPlayerCoatCapsules[1].radius, 18.0F));
        }
        state.axis_by_node[1] = -1;
        const auto kept = motion::step_cloth_node(state, 1U, target, parent, parent, 10.0F, 1.0F);
        assert(near(kept[13], -10.0F));     // not simulated: rest target unchanged
    }

    // TSC (em028_013 layout): two scrolls, linear v and eased u with drift.
    {
        constexpr std::string_view tsc =
            "\r\n.TSC\t\r\n\t# RELATIVE\t\r\n\t\t<Start\r\n\t\t\tScrlNo\t\t0\r\n"
            "\t\t\tScrlType\t1\r\n\t\t\tTexNo\t\t3\r\n\t\t\tDirUV\t\tstay,   up\r\n"
            "\t\t\tTimeUV\t\t0, \t90\r\n\t\tEnd>\r\n\t\t<Start\r\n\t\t\tScrlNo\t\t1\r\n"
            "\t\t\tScrlType\t3\r\n\t\t\tTexNo\t\t2\r\n\t\t\tDirUV\t\tleft, stay\r\n"
            "\t\t\tTimeUV\t\t400,      0\r\n\t\t\tMinimumUV\t0.0001,\t0.005\r\n\t\tEnd>\r\n"
            "\t\t<Finish>\r\n$\t\r\n\t# ABSOLUTE\r\n\t\t<Start ScrlNo 7 ScrlType 1 End>\r\n";
        assert(motion::looks_like_tsc(tsc));
        const auto records = motion::parse_tsc(tsc);
        assert(records.size() == 2U);  // '$' ends the text: ABSOLUTE is never read
        assert(records[0].number == 0U && records[0].type == 1U && records[0].texture == 3);
        assert(records[0].direction[0] == 0 && records[0].direction[1] == 1);
        assert(near(records[0].time[1], 90.0F) && !records[0].has_minimum);
        assert(records[1].type == 3U && records[1].direction[0] == 1 && records[1].has_minimum);
        assert(near(records[1].time[0], 400.0F) && near(records[1].minimum[1], 0.005F));
        // Linear: one full texture per 90 frames, quantized to 1/4096.
        const auto linear = motion::scroll_offset(records[0], 45.0F);
        assert(linear && near((*linear)[0], 0.0F) && near((*linear)[1], 0.5F));
        // Eased: (cos((1 - p) pi) + 1) / 2 plus the MinimumUV drift; v stays.
        const auto eased = motion::scroll_offset(records[1], 200.0F);
        const float expected = std::floor(
            ((std::cos(0.5F * 3.14159265F) + 1.0F) * 0.5F + 0.02F) * 4096.0F) / 4096.0F;
        assert(eased && std::fabs((*eased)[0] - expected) < 0.0005F && near((*eased)[1], 0.0F));
        // Type 4 (0x14030B820): linear, DirUV reverses every TurnTimeUV steps.
        motion::ScrollRecord ping;
        ping.type = 4U;
        ping.direction = {1, 0};
        ping.time = {10.0F, 1.0F};
        ping.turn_time = {5.0F, 5.0F};
        ping.has_turn_time = true;
        assert(near((*motion::scroll_offset(ping, 5.0F))[0], 0.5F));
        assert(std::fabs((*motion::scroll_offset(ping, 7.0F))[0] - 0.3F) < 0.001F);
        assert((*motion::scroll_offset(ping, 10.0F))[0] < 0.001F ||
               (*motion::scroll_offset(ping, 10.0F))[0] > 0.999F);
        // Type 10 (0x14030BB50): (1 - (facing + 1) / 2) * RateUV * dir.
        motion::ScrollRecord facing;
        facing.type = 10U;
        facing.direction = {1, 0};
        facing.rate = {0.5F, 0.0F};
        auto facing_state = motion::start_scroll(facing);
        motion::step_scroll(facing, facing_state, 0.0F);
        assert(facing_state.output[0] == 1024 && facing_state.output[1] == 0);
        assert(!motion::scroll_offset(motion::ScrollRecord{.type = 7U}, 3.0F).has_value());
        assert(motion::object_scroll_number(0x01020000U) == 0);
        assert(motion::object_scroll_number(0x02000000U) == 1);
        assert(motion::object_scroll_number(0x00020000U) == -1);
        assert(!motion::looks_like_tsc(";pl000_02.clt\nClothNo 0\n"));
        assert(motion::tsc_slot_for("st/EM028.PAC", 6U) == 13U);
        assert(!motion::tsc_slot_for("em028.pac", 4U).has_value());
    }

    // Motion script (pl000.pac slot 5): header -> bank list -> MOT scripts;
    // opcode 3 byte 2 low 6 bits = weapon state, opcode 0 = wait for frame.
    {
        const std::vector<std::uint8_t> file{
            0x06, 0x00, 0x00, 0x00, 0x00, 0x00,  // +0: table at 6
            0x04, 0x00,                          // table[0] -> bank list at 6 + 4
            0x00, 0x00,
            0x04, 0x00, 0xFF, 0xFF,              // bank 0 at 10 + 4
            0x02, 0x00,                          // bank 0: MOT 0 script at 14 + 2
            0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0x7F,  // play MOT
            0x03, 0x80, 0x02, 0x00, 0x00, 0x00,  // state 2 (right hand)
            0x00, 0x00, 0x0A, 0x00, 0x00, 0x00,  // wait frame 10
            0x03, 0x80, 0x83, 0x00, 0x00, 0x00,  // state 3 (flag bits dropped)
            0x00, 0x00, 0xFF, 0x7F, 0x00, 0x00,  // until the end
        };
        const auto script = motion::MotionScriptFile::parse(file);
        assert(script && script->bank_count() == 1U);
        const auto keys = script->weapon_states(0U, 0U);
        assert(keys.size() == 2U && keys[0].state == 2U && keys[1].state == 3U);
        assert(motion::weapon_state_at(keys, 0.0F) == 2U);
        assert(motion::weapon_state_at(keys, 10.0F) == 2U);   // runs once past frame 10
        assert(motion::weapon_state_at(keys, 11.0F) == 3U);
        assert(motion::player_motion_bank("motion/PL000_00_13.PAC") == 13U);
        assert(!motion::player_motion_bank("pl000.pac").has_value());
        const auto* hand = motion::weapon_state_record("CPlWpSword", 2U);
        assert(hand != nullptr && hand->joint == 9U && near(hand->translation[0], -7.6F));
        assert(motion::weapon_state_record("CPlWpSword", 3U)->joint == 13U);
        assert(motion::weapon_state_record("CPlWpSword", 7U) == nullptr);   // empty
        assert(motion::weapon_state_record("CPlWpGuitar", 5U) == nullptr);  // play pose
    }

    // The same layout under a non-player name is not guessed at.
    auto enemy = dmcresource::pac_assembly::assemble_pac(*archive, &report, "em001.pac");
    assert(enemy != nullptr && report.attached_parts == 0U);
    assert(!motion::is_attached_part(enemy.get(), 1U));
    return 0;
}
