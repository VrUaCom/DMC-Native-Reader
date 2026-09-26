#include "dmcresource/motion/motion_player.h"
#include "dmcresource/pac_assembly.h"
#include "dmcresource/resource_session.h"

#include <bit>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// PAC regression: relative-slot archive -> typed children -> per-slot opening
// -> read-only assembly (MODs in model space, MOT library, nested PAC).
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

}  // namespace

int main() {
    namespace assembly = dmcresource::pac_assembly;
    const auto mod = make_spatial_mod();
    const auto mot = make_translation_mot();
    const std::vector<std::uint8_t> opaque(32U, 0xABU);

    // Single-model PAC with a motion and an unknown payload.
    const auto pac = make_pac({mod, mot, opaque});
    auto archive = dmcresource::open_session("pl_test.pac", pac.data(), pac.size());
    assert(archive != nullptr);
    assert(archive->probe.format == dmcresource::Format::Pac);
    assert(!archive->renderable);
    assert(archive->children.size() == 3U);
    assert(archive->children[0].suggested_filename == "slot_0000.mod");
    assert(archive->children[1].suggested_filename == "slot_0001.mot");
    assert(archive->children[2].suggested_filename == "slot_0002.bin");

    // Per-file display: each recognized slot opens in its own module.
    auto slot_mod = dmcresource::open_session_child(archive.get(), 0);
    assert(slot_mod != nullptr && slot_mod->renderable);
    auto slot_mot = dmcresource::open_session_child(archive.get(), 1);
    assert(slot_mot != nullptr && slot_mot->probe.format == dmcresource::Format::Mot);
    assert(slot_mot->detail.find("nodes=3") != std::string::npos);

    assembly::AssemblyReport report;
    auto single = assembly::assemble_pac(*archive, &report);
    assert(single != nullptr);
    assert(report.models == 1U && report.motions == 1U);
    assert(single->renderable);
    assert(single->motion_library.size() == 1U);
    assert(single->children.size() == 3U);
    const auto& motion = single->motion_library.front();
    const auto loaded = dmcresource::motion::load_motion(
        single.get(), motion.name, motion.bytes.data(), motion.bytes.size());
    assert(loaded.ok);

    // Nested PAC with two MODs: composite keeps both parts in model space.
    const auto inner = make_pac({mod, mot});
    const auto outer = make_pac({mod, inner});
    auto nested = dmcresource::open_session("em_test.pac", outer.data(), outer.size());
    assert(nested != nullptr);
    auto composite = assembly::assemble_pac(*nested, &report);
    assert(composite != nullptr);
    assert(report.models == 2U && report.nested_archives == 1U && report.motions == 1U);
    assert(composite->composite_parts.size() == 2U);
    assert(!composite->composite_parts[1].placement.resolved);
    const auto& base = slot_mod->render_mesh.vertices;
    for (std::size_t i = 0U; i < base.size(); ++i) {
        assert(composite->render_mesh.vertices[base.size() + i].x == base[i].x);
        assert(composite->render_mesh.vertices[base.size() + i].y == base[i].y);
    }
    // One motion drives both parts because both share the skeleton size.
    const auto& nested_motion = composite->motion_library.front();
    const auto both = dmcresource::motion::load_motion(
        composite.get(), nested_motion.name,
        nested_motion.bytes.data(), nested_motion.bytes.size());
    assert(both.ok && both.animated_parts == 2U);

    // An archive without MOD is browsable but has nothing to assemble.
    const auto motions_only = make_pac({mot});
    auto no_model = dmcresource::open_session("mot.pac", motions_only.data(), motions_only.size());
    assert(no_model != nullptr);
    assert(assembly::assemble_pac(*no_model) == nullptr);
    return 0;
}
