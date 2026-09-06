#include "dmcresource/adapters/mod_adapter.h"

#include <bit>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string_view>
#include <vector>

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

[[nodiscard]] bool near(float a, float b) {
    return std::fabs(a - b) < 0.0001F;
}

[[nodiscard]] bool module_present(
    const dmcresource::PipelineResult& result,
    std::string_view name) {
    for (const auto& module : result.modules) {
        if (module.name != nullptr && name == module.name && module.complete) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] dmcresource::ProbeResult mod_probe() {
    dmcresource::ProbeResult probe;
    probe.format = dmcresource::Format::Mod;
    probe.recognized = true;
    probe.content_confirmed = true;
    probe.family = "MOD";
    probe.domain = "model";
    probe.support = "typed";
    probe.evidence = "exe+corpus";
    probe.mime_type = "application/vnd.dmc.mod";
    return probe;
}

}  // namespace

int main() {
    const auto bytes = make_spatial_mod();
    const auto spatial = dmcresource::adapters::run_mod_adapter(
        mod_probe(), bytes.data(), bytes.size(), "formats.mod.mesh-reader");

    assert(spatial.accepted);
    assert(spatial.renderable);
    assert(spatial.scene.nodes.size() == 3U);
    assert(spatial.scene.nodes[0].parent == -1);
    assert(spatial.scene.nodes[2].parent == 0);
    assert(spatial.scene.nodes[1].parent == 2);

    // Canonical model-space world propagation:
    // node0=(10,0,0), node2=(10,5,0), node1=(10,5,2).
    assert(near(spatial.scene.nodes[0].world.values[12], 10.0F));
    assert(near(spatial.scene.nodes[0].world.values[13], 0.0F));
    assert(near(spatial.scene.nodes[0].world.values[14], 0.0F));
    assert(near(spatial.scene.nodes[2].world.values[12], 10.0F));
    assert(near(spatial.scene.nodes[2].world.values[13], 5.0F));
    assert(near(spatial.scene.nodes[2].world.values[14], 0.0F));
    assert(near(spatial.scene.nodes[1].world.values[12], 10.0F));
    assert(near(spatial.scene.nodes[1].world.values[13], 5.0F));
    assert(near(spatial.scene.nodes[1].world.values[14], 2.0F));
    assert(near(spatial.scene.nodes[1].local.values[14], 2.0F));
    assert(module_present(spatial, "canonical.mod.spatial-hierarchy"));
    assert(spatial.detail.find("spatialHierarchy=yes") != std::string::npos);

    // Concrete-document gate: a non-finite local transform keeps the MOD
    // inspectable/renderable but must not publish fabricated spatial matrices.
    auto non_spatial_bytes = bytes;
    put_f32(non_spatial_bytes, 0x270U,
            std::numeric_limits<float>::quiet_NaN());
    const auto non_spatial = dmcresource::adapters::run_mod_adapter(
        mod_probe(), non_spatial_bytes.data(), non_spatial_bytes.size(),
        "formats.mod.mesh-reader");

    assert(non_spatial.accepted);
    assert(non_spatial.renderable);
    assert(non_spatial.scene.nodes.size() == 3U);
    assert(non_spatial.scene.nodes[2].parent == 0);
    assert(non_spatial.scene.nodes[1].parent == 2);
    assert(!module_present(non_spatial, "canonical.mod.spatial-hierarchy"));
    assert(non_spatial.detail.find("spatialHierarchy=no") != std::string::npos);
    assert(near(non_spatial.scene.nodes[0].world.values[12], 0.0F));
    assert(near(non_spatial.scene.nodes[2].world.values[13], 0.0F));
    assert(near(non_spatial.scene.nodes[1].world.values[14], 0.0F));

    return 0;
}
