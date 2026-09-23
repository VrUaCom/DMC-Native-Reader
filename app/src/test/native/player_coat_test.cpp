#include "dmcresource/motion/motion_player.h"
#include "dmcresource/motion/part_attachment.h"
#include "dmcresource/pac_assembly.h"
#include "dmcresource/resource_session.h"

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

    // The same layout under a non-player name is not guessed at.
    auto enemy = dmcresource::pac_assembly::assemble_pac(*archive, &report, "em001.pac");
    assert(enemy != nullptr && report.attached_parts == 0U);
    assert(!motion::is_attached_part(enemy.get(), 1U));
    return 0;
}
