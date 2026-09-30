#include "dmcresource/decode_pipeline.h"
#include "dmcresource/resource_capabilities.h"
#include "dmcresource/model_texture_binding.h"
#include "dmcresource/spider/black_widow.h"
#include "dmcresource/view_renderer.h"
#include "dmcresource/neutral_texture.h"
#include "dmcresource/resource_session.h"
#include "dmcresource/session_inspection.h"
#include "dmcresource/stage_room.h"
#include "dmcresource/environment_collision.h"
#include "dmcresource/inspection_format.h"

#include <algorithm>
#include <bit>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace {

void put_u8(std::vector<std::uint8_t>& bytes, std::size_t offset,
            std::uint8_t value) {
    assert(offset < bytes.size());
    bytes[offset] = value;
}

void put_u16(std::vector<std::uint8_t>& bytes, std::size_t offset,
             std::uint16_t value) {
    assert(offset + 2U <= bytes.size());
    put_u8(bytes, offset + 0U, static_cast<std::uint8_t>(value & 0xFFU));
    put_u8(bytes, offset + 1U,
           static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
}

void put_u32(std::vector<std::uint8_t>& bytes, std::size_t offset,
             std::uint32_t value) {
    assert(offset + 4U <= bytes.size());
    for (std::size_t i = 0U; i < 4U; ++i) {
        put_u8(bytes, offset + i,
               static_cast<std::uint8_t>((value >> (i * 8U)) & 0xFFU));
    }
}

void put_u64(std::vector<std::uint8_t>& bytes, std::size_t offset,
             std::uint64_t value) {
    assert(offset + 8U <= bytes.size());
    for (std::size_t i = 0U; i < 8U; ++i) {
        put_u8(bytes, offset + i,
               static_cast<std::uint8_t>((value >> (i * 8U)) & 0xFFU));
    }
}

void put_f32(std::vector<std::uint8_t>& bytes, std::size_t offset,
             float value) {
    put_u32(bytes, offset, std::bit_cast<std::uint32_t>(value));
}

std::vector<std::uint8_t> make_mod() {
    std::vector<std::uint8_t> bytes(0x240U, 0U);
    bytes[0] = 'M'; bytes[1] = 'O'; bytes[2] = 'D'; bytes[3] = ' ';
    put_f32(bytes, 0x04U, 1.01F);
    put_u8(bytes, 0x10U, 1U);
    put_u8(bytes, 0x11U, 1U);
    put_u64(bytes, 0x20U, 0x200U);

    put_u8(bytes, 0x40U, 1U);
    put_u16(bytes, 0x42U, 3U);
    put_u64(bytes, 0x48U, 0x80U);

    put_u16(bytes, 0x80U, 3U);
    put_u16(bytes, 0x82U, 5U);
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

    put_f32(bytes, 0xD0U, 0.0F); put_f32(bytes, 0xD4U, 0.0F); put_f32(bytes, 0xD8U, 0.0F);
    put_f32(bytes, 0xDCU, 1.0F); put_f32(bytes, 0xE0U, 0.0F); put_f32(bytes, 0xE4U, 0.0F);
    put_f32(bytes, 0xE8U, 0.0F); put_f32(bytes, 0xECU, 1.0F); put_f32(bytes, 0xF0U, 0.0F);

    for (std::size_t i = 0U; i < 3U; ++i) {
        const auto n = 0x100U + i * 12U;
        put_f32(bytes, n + 0U, 0.0F);
        put_f32(bytes, n + 4U, 0.0F);
        put_f32(bytes, n + 8U, 1.0F);
    }

    put_u16(bytes, 0x130U, 0U);    put_u16(bytes, 0x132U, 0U);
    put_u16(bytes, 0x134U, 4096U); put_u16(bytes, 0x136U, 0U);
    put_u16(bytes, 0x138U, 0U);    put_u16(bytes, 0x13AU, 4096U);

    put_u16(bytes, 0x150U, 0x001FU);
    put_u16(bytes, 0x152U, 0x001FU);
    put_u16(bytes, 0x154U, 0x001FU);

    put_u32(bytes, 0x200U, 0x10U);
    put_u32(bytes, 0x204U, 0x20U);
    put_u32(bytes, 0x208U, 0x30U);
    put_u8(bytes, 0x210U, 0xFFU);
    put_u8(bytes, 0x220U, 0U);
    put_u8(bytes, 0x230U, 0U);
    return bytes;
}

std::vector<std::uint8_t> make_scm() {
    std::vector<std::uint8_t> bytes(0x1B0U, 0U);
    bytes[0] = 'S'; bytes[1] = 'C'; bytes[2] = 'M'; bytes[3] = ' ';
    put_f32(bytes, 0x04U, 1.01F);
    put_u8(bytes, 0x10U, 1U);
    put_u8(bytes, 0x11U, 1U);
    put_u8(bytes, 0x12U, 1U);
    put_u32(bytes, 0x14U, 300100U);
    put_u64(bytes, 0x20U, 0x150U);

    put_u8(bytes, 0x40U, 1U);
    put_u8(bytes, 0x41U, 0x80U);
    put_u16(bytes, 0x42U, 3U);
    put_u64(bytes, 0x48U, 0x80U);
    put_u32(bytes, 0x50U, 0x00004000U);
    put_f32(bytes, 0x70U, 0.5F);
    put_f32(bytes, 0x74U, 0.5F);
    put_f32(bytes, 0x78U, 0.0F);
    put_f32(bytes, 0x7CU, 1.0F);

    put_u16(bytes, 0x80U, 3U);
    put_u16(bytes, 0x82U, 0U);
    put_u16(bytes, 0x84U, 1U);
    put_u16(bytes, 0x86U, 2U);
    put_u16(bytes, 0x88U, 3U);
    put_u16(bytes, 0x8AU, 4U);
    put_u64(bytes, 0x90U, 0xD0U);
    put_u64(bytes, 0x98U, 0x100U);
    put_u64(bytes, 0xA0U, 0x130U);
    put_u64(bytes, 0xA8U, 0U);
    put_u64(bytes, 0xB8U, 0x140U);
    put_u64(bytes, 0xC0U, 0x120U);

    put_f32(bytes, 0xD0U, 0.0F); put_f32(bytes, 0xD4U, 0.0F); put_f32(bytes, 0xD8U, 0.0F);
    put_f32(bytes, 0xDCU, 1.0F); put_f32(bytes, 0xE0U, 0.0F); put_f32(bytes, 0xE4U, 0.0F);
    put_f32(bytes, 0xE8U, 0.0F); put_f32(bytes, 0xECU, 1.0F); put_f32(bytes, 0xF0U, 0.0F);

    for (std::size_t i = 0U; i < 3U; ++i) {
        const auto n = 0x100U + i * 12U;
        put_f32(bytes, n + 0U, 0.0F);
        put_f32(bytes, n + 4U, 0.0F);
        put_f32(bytes, n + 8U, 1.0F);
    }

    put_u16(bytes, 0x130U, 0U);    put_u16(bytes, 0x132U, 0U);
    put_u16(bytes, 0x134U, 4096U); put_u16(bytes, 0x136U, 0U);
    put_u16(bytes, 0x138U, 0U);    put_u16(bytes, 0x13AU, 4096U);

    bytes[0x140U] = 255U; bytes[0x141U] = 0U;   bytes[0x142U] = 0U;   bytes[0x143U] = 0U;
    bytes[0x144U] = 0U;   bytes[0x145U] = 255U; bytes[0x146U] = 0U;   bytes[0x147U] = 0U;
    bytes[0x148U] = 0U;   bytes[0x149U] = 0U;   bytes[0x14AU] = 255U; bytes[0x14BU] = 0U;

    put_u32(bytes, 0x150U, 0x20U);
    put_u32(bytes, 0x154U, 0x24U);
    put_u32(bytes, 0x158U, 0x28U);
    put_u32(bytes, 0x15CU, 0x30U);
    put_u8(bytes, 0x170U, 0xFFU);
    put_u8(bytes, 0x174U, 0U);
    put_u8(bytes, 0x178U, 0U);

    put_f32(bytes, 0x180U, 10.0F);
    put_f32(bytes, 0x184U, 20.0F);
    put_f32(bytes, 0x188U, 30.0F);
    put_f32(bytes, 0x18CU, std::sqrt(1400.0F));
    put_f32(bytes, 0x190U, 0.0F);
    put_f32(bytes, 0x194U, 0.0F);
    put_f32(bytes, 0x198U, 0.0F);
    put_f32(bytes, 0x19CU, 0.0F);

    put_u16(bytes, 0x1A0U, 0x1212U);
    return bytes;
}

std::vector<std::uint8_t> make_hits() {
    // One valid HITS cell and one triangle-plane record.  The fixture checks
    // the environment-collision boundary without assigning surface semantics
    // to raw flags.
    std::vector<std::uint8_t> bytes(0x88U, 0U);
    bytes[0] = 'H'; bytes[1] = 'I'; bytes[2] = 'T'; bytes[3] = 'S';
    put_u32(bytes, 0x04U, 0x88U);
    put_f32(bytes, 0x08U, 0.0F);   put_f32(bytes, 0x0CU, 0.0F);   put_f32(bytes, 0x10U, 0.0F);
    put_f32(bytes, 0x14U, 100.0F); put_f32(bytes, 0x18U, 100.0F); put_f32(bytes, 0x1CU, 100.0F);
    put_f32(bytes, 0x20U, 100.0F); put_f32(bytes, 0x24U, 100.0F); put_f32(bytes, 0x28U, 100.0F);
    put_u32(bytes, 0x2CU, 1U); put_u32(bytes, 0x30U, 1U); put_u32(bytes, 0x34U, 1U);
    put_u32(bytes, 0x38U, 1U); put_u32(bytes, 0x3CU, 0x3CU); put_u32(bytes, 0x40U, 0x48U);
    put_u32(bytes, 0x44U, 0x40U);  // cell list at file offset 0x48
    put_u32(bytes, 0x48U, 0U);      // triangle byte offset 0
    put_u32(bytes, 0x4CU, 0xFFFFFFFFU);
    put_u32(bytes, 0x50U, 0x12345678U);
    put_f32(bytes, 0x54U, 0.0F);  put_f32(bytes, 0x58U, 0.0F);  put_f32(bytes, 0x5CU, 0.0F);
    put_f32(bytes, 0x60U, 10.0F); put_f32(bytes, 0x64U, 0.0F);  put_f32(bytes, 0x68U, 0.0F);
    put_f32(bytes, 0x6CU, 0.0F);  put_f32(bytes, 0x70U, 0.0F);  put_f32(bytes, 0x74U, 10.0F);
    put_f32(bytes, 0x78U, 0.0F);  put_f32(bytes, 0x7CU, 1.0F);  put_f32(bytes, 0x80U, 0.0F);
    put_f32(bytes, 0x84U, 0.0F);
    return bytes;
}

std::vector<std::uint8_t> make_stage_pac(const std::vector<std::uint8_t>& scm,
                                         const std::vector<std::uint8_t>& hits) {
    // PAC offsets are relative physical slot starts.  Keeping the HITS in a
    // separate slot exercises the same provenance path as a real stage PAC.
    constexpr std::size_t scm_offset = 0x10U;
    constexpr std::size_t hits_offset = 0x1C0U;
    std::vector<std::uint8_t> bytes(hits_offset + hits.size(), 0U);
    bytes[0] = 'P'; bytes[1] = 'A'; bytes[2] = 'C'; bytes[3] = 0U;
    put_u32(bytes, 0x04U, 2U);
    put_u32(bytes, 0x08U, static_cast<std::uint32_t>(scm_offset));
    put_u32(bytes, 0x0CU, static_cast<std::uint32_t>(hits_offset));
    assert(scm.size() == hits_offset - scm_offset);
    std::copy(scm.begin(), scm.end(), bytes.begin() + static_cast<std::ptrdiff_t>(scm_offset));
    std::copy(hits.begin(), hits.end(), bytes.begin() + static_cast<std::ptrdiff_t>(hits_offset));
    return bytes;
}

bool trace_contains(const dmcresource::PipelineResult& result,
                    std::string_view id) {
    for (const auto& module : result.modules) {
        if (module.name != nullptr && std::string_view{module.name} == id &&
            module.complete) {
            return true;
        }
    }
    return false;
}

void assert_unit_uv_triangle(const dmcresource::Mesh& mesh) {
    assert(mesh.has_uv0());
    assert(mesh.uv0.size() == 3U);
    assert(std::fabs(mesh.uv0[0].u - 0.0F) < 0.0001F);
    assert(std::fabs(mesh.uv0[0].v - 0.0F) < 0.0001F);
    assert(std::fabs(mesh.uv0[1].u - 1.0F) < 0.0001F);
    assert(std::fabs(mesh.uv0[1].v - 0.0F) < 0.0001F);
    assert(std::fabs(mesh.uv0[2].u - 0.0F) < 0.0001F);
    assert(std::fabs(mesh.uv0[2].v - 1.0F) < 0.0001F);
}

}  // namespace

int main() {
    using dmcresource::Format;
    using dmcresource::ResourceCapability;
    using dmcresource::has_capability;
    using dmcresource::run_decode_pipeline;

    const auto scm = make_scm();
    const auto scm_result = run_decode_pipeline(
        "sample.scm", scm.data(), scm.size());
    assert(scm_result.accepted);
    assert(scm_result.renderable);
    assert(scm_result.probe.format == Format::Scm);
    assert(trace_contains(scm_result, "canonical.scm.structural-parser"));
    assert(trace_contains(scm_result, "canonical.scm.scene-hierarchy"));
    assert(trace_contains(scm_result, "native.uv-projection"));
    assert(trace_contains(scm_result, "render-scene-contract"));
    assert(scm_result.scene.meshes.size() == 1U);
    assert(scm_result.scene.meshes[0].node_index == 0);
    assert(scm_result.scene.meshes[0].mesh.vertices.size() == 3U);
    assert(scm_result.scene.meshes[0].mesh.indices.size() == 3U);
    assert_unit_uv_triangle(scm_result.scene.meshes[0].mesh);
    assert(scm_result.scene.nodes.size() == 1U);
    assert(scm_result.scene.nodes[0].parent == -1);
    assert(std::fabs(scm_result.scene.nodes[0].local.values[12] - 10.0F) < 0.0001F);
    assert(std::fabs(scm_result.scene.nodes[0].world.values[13] - 20.0F) < 0.0001F);
    assert(scm_result.scene.textures.size() == 1U);
    assert(scm_result.scene.textures[0].texture_slot == 0U);
    assert(!scm_result.inspection.empty());
    assert(scm_result.inspection.format == "SCM");
    assert(has_capability(scm_result.capabilities,
                          ResourceCapability::TextureBinding));
    assert(has_capability(scm_result.capabilities,
                          ResourceCapability::UvCoordinates));

    const auto mod = make_mod();
    const auto mod_result = run_decode_pipeline(
        "sample.mod", mod.data(), mod.size());
    assert(mod_result.accepted);
    assert(mod_result.renderable);
    assert(mod_result.probe.format == Format::Mod);
    assert(trace_contains(mod_result, "canonical.mod.structural-parser"));
    assert(trace_contains(mod_result, "canonical.mod.texture-state"));
    assert(trace_contains(mod_result, "native.uv-projection"));
    assert(trace_contains(mod_result, "render-scene-contract"));
    assert(mod_result.scene.meshes.size() == 1U);
    assert(mod_result.scene.meshes[0].mesh.vertices.size() == 3U);
    assert(mod_result.scene.meshes[0].mesh.indices.size() == 3U);
    assert_unit_uv_triangle(mod_result.scene.meshes[0].mesh);
    assert(mod_result.scene.nodes.size() == 1U);
    assert(mod_result.scene.skins.size() == 1U);
    assert(mod_result.scene.skins[0].vertices.size() == 3U);
    for (const auto& vertex : mod_result.scene.skins[0].vertices) {
        assert(vertex.influences.size() == 1U);
        assert(vertex.influences[0].node_index == 0U);
        assert(vertex.influences[0].weight > 0.999F);
    }
    assert(mod_result.scene.textures.size() == 1U);
    assert(mod_result.scene.textures[0].texture_slot == 5U);
    assert(!mod_result.inspection.empty());
    assert(mod_result.inspection.format == "MOD");
    assert(has_capability(mod_result.capabilities,
                          ResourceCapability::SkinWeights));
    assert(has_capability(mod_result.capabilities,
                          ResourceCapability::UvCoordinates));

    const auto hits = make_hits();
    const auto hits_result = run_decode_pipeline(
        "stage.hits", hits.data(), hits.size());
    assert(hits_result.accepted && hits_result.renderable);  // drawn as a 3D surface
    assert(hits_result.scene.meshes.size() == 1U && hits_result.scene.meshes[0].mesh.indices.size() == 3U);
    assert(hits_result.probe.format == Format::Hits);
    assert(hits_result.inspection.format == "HITS");
    assert(has_capability(hits_result.capabilities, ResourceCapability::Collision));
    const auto collision = dmcresource::environment_collision::parse(
        "slot_0003.hits", 3U,
        std::span<const std::uint8_t>{hits.data(), hits.size()});
    assert(collision && collision->resource_slot == 3U && collision->triangles.size() == 1U);
    assert(collision->cell_reference_count == 1U);
    assert(dmcresource::environment_collision::debug_lines(*collision).size() == 6U);

    // Stage collision queries (0x14005E880 segment test and the Reader's
    // character proxy) on a floor y = 0 and a wall x = 100 facing -x.
    {
        namespace ec = dmcresource::environment_collision;
        using dmcresource::Vec3;
        ec::Source source;
        source.bounds_min = {-500.0F, -10.0F, -500.0F};
        source.bounds_max = {500.0F, 500.0F, 500.0F};
        // Floor (normal +y, plane y = 0), two triangles.
        source.triangles.push_back({0U, {-500.0F, 0.0F, -500.0F}, {-500.0F, 0.0F, 500.0F},
                                    {500.0F, 0.0F, 500.0F}, {0.0F, 1.0F, 0.0F}, 0.0F});
        source.triangles.push_back({0U, {-500.0F, 0.0F, -500.0F}, {500.0F, 0.0F, 500.0F},
                                    {500.0F, 0.0F, -500.0F}, {0.0F, 1.0F, 0.0F}, 0.0F});
        // Wall x = 100 (normal -x: n.p + d = 0 with d = 100), category 0x0002.
        source.triangles.push_back({0x00020000U, {100.0F, 0.0F, -500.0F}, {100.0F, 500.0F, -500.0F},
                                    {100.0F, 0.0F, 500.0F}, {-1.0F, 0.0F, 0.0F}, 100.0F});
        source.triangles.push_back({0x00020000U, {100.0F, 500.0F, -500.0F}, {100.0F, 500.0F, 500.0F},
                                    {100.0F, 0.0F, 500.0F}, {-1.0F, 0.0F, 0.0F}, 100.0F});

        // Kinds: one per distinct flags value, ascending; floors/walls counted.
        {
            const auto kinds = ec::kinds(source);
            assert(kinds.size() == 2U && kinds[0].flags == 0U && kinds[0].count == 2U &&
                   kinds[0].floors == 2U && kinds[1].flags == 0x00020000U && kinds[1].walls == 2U);
            const auto line_kinds = ec::debug_line_kinds(source, kinds);
            assert(line_kinds.size() == 12U && line_kinds[0] == 0U && line_kinds[6] == 1U);
            assert(dmcresource::collision_kind_color(0) != dmcresource::collision_kind_color(1));
        }

        // Nearest hit wins: a diagonal shot meets the wall before the floor.
        const auto wall = ec::segment_hit(source, {0.0F, 100.0F, 0.0F}, {200.0F, -100.0F, 0.0F});
        assert(wall && wall->triangle >= 2U);
        assert(std::fabs(wall->point.x - 100.0F) < 1.0e-3F && std::fabs(wall->point.y) < 1.0e-3F + 1.0F);
        assert(std::fabs(wall->fraction - 0.5F) < 1.0e-4F);
        // A category mask skips the wall records.
        const auto masked = ec::segment_hit(source, {0.0F, 100.0F, 0.0F}, {200.0F, 50.0F, 0.0F}, 0x0002U);
        assert(!masked);
        const auto floor = ec::segment_hit(source, {0.0F, 50.0F, 0.0F}, {0.0F, -50.0F, 0.0F});
        assert(floor && std::fabs(floor->point.y) < 1.0e-4F && floor->normal.y == 1.0F);
        assert(!ec::segment_hit(source, {0.0F, 50.0F, 0.0F}, {50.0F, 60.0F, 0.0F}));

        // The sphere stops `radius` short of the wall and slides along it.
        const auto slid = ec::slide_sphere(source, {0.0F, 90.0F, 0.0F}, {300.0F, 90.0F, 40.0F}, 50.0F);
        assert(std::fabs(slid.x - 50.0F) < 0.5F && std::fabs(slid.z - 40.0F) < 0.5F);
        const auto free_move = ec::slide_sphere(source, {0.0F, 90.0F, 0.0F}, {-200.0F, 90.0F, 0.0F}, 50.0F);
        assert(std::fabs(free_move.x + 200.0F) < 1.0e-3F);
        const auto height = ec::floor_below(source, {10.0F, 90.0F, 10.0F}, 1000.0F);
        assert(height && std::fabs(*height) < 1.0e-4F);
        assert(!ec::floor_below(source, {10.0F, -5.0F, 10.0F}, 1000.0F));

        // Room <-> model placement round trip (view_renderer's room pass).
        namespace room = dmcresource::stage_room;
        const room::Placement placement{{10.0F, 2.0F, -4.0F}, 0.7F, {3.0F, -1.0F, 5.0F}};
        const Vec3 p{123.0F, 45.0F, -67.0F};
        const auto back = room::model_to_room(placement, room::room_to_model(placement, p));
        assert(std::fabs(back.x - p.x) < 1.0e-3F && std::fabs(back.y - p.y) < 1.0e-3F &&
               std::fabs(back.z - p.z) < 1.0e-3F);
    }

    // Both canonical adapters must preserve slots through render materialization
    // and expose the native companion action (including MOD's nonzero slot 5).
    for (const auto* result : {&scm_result, &mod_result}) {
        dmcresource::Mesh mesh;
        std::vector<std::uint32_t> slots;
        assert(dmcresource::materialize_render_scene(result->scene, &mesh));
        assert(dmcresource::materialize_triangle_texture_slots(result->scene, &slots));
        dmcresource::model_texture_binding::RequiredSlots required;
        assert(dmcresource::model_texture_binding::collect_required_slots(mesh, slots, &required));
        assert(required.slots.size() == 1U);
        assert(required.slots[0] == result->scene.textures[0].texture_slot);
        namespace widow = dmcresource::spider::black_widow;
        const auto state = widow::evaluate_model_session({
            .capabilities = result->capabilities,
            .renderable = result->renderable,
            .render_mesh = &mesh,
            .triangle_texture_slots = slots,
        });
        assert(widow::has_state(state, widow::StateFlag::TextureCompanionAttachable));
    }

    // Viewer room (stage_room.h): a lone SCM is a room; a stage never gets one;
    // floor spots stand on the centre of the floor; the room pass paints the
    // background around a model and hides the wall facing away from the camera.
    {
        namespace room = dmcresource::stage_room;
        const auto built = room::build_room("sample.scm", scm.data(), scm.size());
        assert(built && built->pieces == 1U && built->mesh.indices.size() == 3U);
        assert(built->triangle_texture_slots.size() == 1U && !built->spots.empty());
        const auto stage_pac = make_stage_pac(scm, hits);
        const auto built_stage = room::build_room("stage.pac", stage_pac.data(), stage_pac.size());
        assert(built_stage && built_stage->pieces == 1U);
        assert(built_stage->collision_sources.size() == 1U);
        assert(built_stage->collision_sources[0].resource_slot == 1U);
        assert(built_stage->collision_sources[0].triangles.size() == 1U);
        assert(built_stage->collision_lines.size() == 6U);
        const auto stage = dmcresource::open_session("sample.scm", scm.data(), scm.size());
        const auto model = dmcresource::open_session("sample.mod", mod.data(), mod.size());
        assert(stage && room::is_stage_session(*stage) && model && !room::is_stage_session(*model));
        assert(!room::build_room("x.bin", mod.data(), 8U));

        dmcresource::Mesh box;
        const auto quad = [&box](dmcresource::Vec3 a, dmcresource::Vec3 b, dmcresource::Vec3 c,
                                 dmcresource::Vec3 d, dmcresource::Vec3 n) {
            const auto base = static_cast<std::uint32_t>(box.vertices.size());
            for (const auto& v : {a, b, c, d}) {
                box.vertices.push_back(v);
                box.normal0.push_back(n);
            }
            for (const auto i : {0U, 1U, 2U, 0U, 2U, 3U}) box.indices.push_back(base + i);
        };
        quad({-500, 0, -500}, {500, 0, -500}, {500, 0, 500}, {-500, 0, 500}, {0, 1, 0});   // floor
        quad({-500, 0, 500}, {500, 0, 500}, {500, 400, 500}, {-500, 400, 500}, {0, 0, -1});  // far wall
        quad({-500, 0, -500}, {-500, 400, -500}, {500, 400, -500}, {500, 0, -500}, {0, 0, 1});  // near wall
        const auto spots = room::floor_spots(box);
        assert(!spots.empty() && std::fabs(spots[0].x) < 1.0F && std::fabs(spots[0].y) < 1.0F &&
               std::fabs(spots[0].z) < 1.0F);
        const auto under = room::floor_spots_near(box, {100.0F, 300.0F, 100.0F});
        assert(under && std::fabs(under->y) < 1.0F);
        assert(!room::floor_spots_near(box, {0.0F, -10.0F, 0.0F}));  // nothing below

        dmcresource::ViewState view;
        view.yaw_radians = 0.0F;
        view.pitch_radians = -0.3F;
        const auto bare = dmcresource::render_view(model->render_mesh, 128, 128, view);
        view.room_mesh = &box;
        view.room_offset = {0.0F, -50.0F, 0.0F};
        const auto roomed = dmcresource::render_view(model->render_mesh, 128, 128, view);
        std::size_t changed = 0U;
        for (std::size_t o = 0U; o < bare.pixels.size(); o += 4U) {
            if (bare.pixels[o] != roomed.pixels[o]) ++changed;
        }
        assert(changed > 128U * 128U / 4U);  // floor and far wall behind the model

        // Gesture picks: the model under the view centre; the room floor under
        // the bottom of the view (room point on the floor, y = 0 in the room).
        {
            dmcresource::Mesh square;
            square.vertices = {{-20, 0, 0}, {20, 0, 0}, {20, 40, 0}, {-20, 40, 0}};
            square.indices = {0, 1, 2, 0, 2, 3};
            dmcresource::ViewState look;
            look.yaw_radians = 0.0F;
            look.pitch_radians = -0.3F;
            const auto centre = dmcresource::pick_view(square, 200, 200, look, 100.0F, 100.0F);
            assert(centre.model && !centre.room);
            assert(!dmcresource::pick_view(square, 200, 200, look, 2.0F, 2.0F).model);
            look.room_mesh = &box;
            look.room_offset = {0.0F, 0.0F, 0.0F};
            const auto floor = dmcresource::pick_view(square, 200, 200, look, 100.0F, 190.0F);
            assert(floor.room && floor.room_floor && !floor.model && std::fabs(floor.room_point.y) < 0.5F);
            const auto wall = dmcresource::pick_view(square, 200, 200, look, 100.0F, 8.0F);
            assert(!wall.room || !wall.room_floor);  // the far wall is no floor
            // Pan moves the picture: the centre pixel no longer hits the model.
            look.pan_x = 3.0F;
            assert(!dmcresource::pick_view(square, 200, 200, look, 100.0F, 100.0F).model);
            look.pan_x = 0.0F;
            // Turning the room about its pivot changes what is drawn.
            const auto before = dmcresource::render_view(square, 96, 96, look);
            look.room_yaw = 0.8F;
            const auto after = dmcresource::render_view(square, 96, 96, look);
            assert(before.pixels != after.pixels);
        }

        // A soft-alpha texture also holds opaque texels: a triangle flagged
        // translucent still draws them (a distant tower's walls), and the
        // wireframe view outlines the room's meshes.
        {
            dmcresource::Mesh wall;
            wall.vertices = {{-300, 0, 400}, {300, 0, 400}, {300, 500, 400}, {-300, 500, 400}};
            wall.uv0 = {{0, 1}, {1, 1}, {1, 0}, {0, 0}};
            wall.indices = {0, 1, 2, 0, 2, 3};
            dmcresource::ImagePreview solid;
            solid.width = 2U;
            solid.height = 2U;
            solid.rgba8.assign(16U, 255U);  // opaque white
            const std::vector<std::uint32_t> slots(2U, 0U);
            const std::vector<dmcresource::ImagePreview> textures{solid};
            const std::vector<std::uint8_t> soft(2U, 1U);  // flagged translucent
            dmcresource::ViewState look;
            look.yaw_radians = 0.0F;
            look.pitch_radians = 0.0F;
            const auto bare = dmcresource::render_view(model->render_mesh, 96, 96, look);
            look.room_mesh = &wall;
            look.room_texture_slots = &slots;
            look.room_textures = &textures;
            look.room_translucent_triangles = &soft;
            const auto drawn = dmcresource::render_view(model->render_mesh, 96, 96, look);
            std::size_t lit = 0U;
            for (std::size_t o = 0U; o < drawn.pixels.size(); o += 4U) {
                if (drawn.pixels[o] != bare.pixels[o]) ++lit;
            }
            assert(lit > 96U * 96U / 8U);
            look.wireframe = true;
            look.room_wire_main = true;
            const auto wired = dmcresource::render_view(model->render_mesh, 96, 96, look);
            std::size_t lines = 0U;
            for (std::size_t o = 0U; o < wired.pixels.size(); o += 4U) {
                if (wired.pixels[o] > 120U && wired.pixels[o + 2U] > 200U) ++lines;
            }
            assert(lines > 20U);
            // A wider mesh line covers more pixels.
            look.mesh_line_px = 4;
            const auto thick = dmcresource::render_view(model->render_mesh, 96, 96, look);
            std::size_t thick_lines = 0U;
            for (std::size_t o = 0U; o < thick.pixels.size(); o += 4U) {
                if (thick.pixels[o] > 120U && thick.pixels[o + 2U] > 200U) ++thick_lines;
            }
            assert(thick_lines > lines * 2U);
        }

        // Room normals are not trusted: a wall whose vertex normals point away
        // from the camera is still drawn (whole far buildings did this).
        {
            dmcresource::Mesh wall;
            wall.vertices = {{-300, 0, 400}, {300, 0, 400}, {300, 500, 400}, {-300, 500, 400}};
            wall.normal0.assign(4U, {0.0F, 0.0F, 1.0F});  // away from the camera at z < 400
            wall.indices = {0, 1, 2, 0, 2, 3};
            dmcresource::ViewState look;
            look.yaw_radians = 0.0F;
            look.pitch_radians = 0.0F;
            const auto bare = dmcresource::render_view(model->render_mesh, 96, 96, look);
            look.room_mesh = &wall;
            const auto drawn = dmcresource::render_view(model->render_mesh, 96, 96, look);
            std::size_t lit = 0U;
            for (std::size_t o = 0U; o < drawn.pixels.size(); o += 4U) {
                if (drawn.pixels[o] != bare.pixels[o]) ++lit;
            }
            assert(lit > 96U * 96U / 8U);
        }

        // Settings bits: background 2 (light) fills the corner.
        const auto light = dmcresource::render_session(model.get(), 64, 64, 0.0F, 0.0F, 1.0F,
            2U << dmcresource::kRenderBackgroundShift);
        assert(light.pixels[0] == 196U && light.pixels[1] == 198U && light.pixels[2] == 204U);

        // Right-handed data: with the camera looking down +Z, a point on +X
        // shows left of centre (the viewer mirrors X; a sword in the right
        // hand, body joint 9 on -X, is on the model's right side).
        dmcresource::Mesh marker;
        // A square at the origin and a thin marker above it on +X.
        marker.vertices = {{-10, -10, 0}, {10, -10, 0}, {-10, 10, 0}, {100, 30, 0}, {110, 30, 0}, {100, 32, 0}};
        marker.indices = {0, 1, 2, 3, 4, 5};
        dmcresource::ViewState straight;
        straight.yaw_radians = 0.0F;
        straight.pitch_radians = 0.0F;
        const auto seen = dmcresource::render_view(marker, 200, 200, straight);
        double square_x = 0.0, marker_x = 0.0;
        std::size_t square_n = 0U, marker_n = 0U;
        for (int y = 0; y < 200; ++y) {
            for (int x = 0; x < 200; ++x) {
                const auto o = static_cast<std::size_t>(y * 200 + x) * 4U;
                if (seen.pixels[o] == 18U && seen.pixels[o + 2U] == 22U) continue;  // background
                if (y < 100) {
                    marker_x += x;
                    ++marker_n;
                } else {
                    square_x += x;
                    ++square_n;
                }
            }
        }
        assert(marker_n > 0U && square_n > 0U);
        assert(marker_x / static_cast<double>(marker_n) < square_x / static_cast<double>(square_n));
    }

    // A known old-family filename must remain outside the clean main surface.
    for (const auto* bytes : {&scm, &mod}) {
        const auto session = dmcresource::open_session(
            bytes == &scm ? "sample.scm" : "sample.mod", bytes->data(), bytes->size());
        assert(session && session->renderable);
        namespace widow = dmcresource::spider::black_widow;
        assert(widow::has_state(dmcresource::black_widow_state(session.get()),
                               widow::StateFlag::TextureCompanionAttachable));
        assert(session->render_triangle_texture_slots[0] == (bytes == &scm ? 0U : 5U));
        // Untextured geometry renders with the generated neutral texture
        // (at.ptx stand-in: 128x64 flat 0x80), lit by the camera light.
        dmcresource::ViewState view;
        view.fallback_texture = &dmcresource::neutral_texture();
        const auto direct = dmcresource::render_view(session->render_mesh, 128, 128, view);
        const auto via_session = dmcresource::render_session(session.get(), 128, 128,
            view.yaw_radians, view.pitch_radians, view.zoom, 0U);
        assert(via_session.pixels == direct.pixels);
        const auto& neutral = dmcresource::neutral_texture();
        assert(neutral.width == 128U && neutral.height == 64U && neutral.available());
        assert(neutral.rgba8[0] == 0x80U && neutral.rgba8[1] == 0x80U && neutral.rgba8[2] == 0x80U &&
               neutral.rgba8[3] == 0xFFU && neutral.rgba8[neutral.rgba8.size() - 4U] == 0x80U);
        std::size_t grey = 0U;
        for (std::size_t o = 0U; o + 3U < via_session.pixels.size(); o += 4U) {
            const auto r = via_session.pixels[o], g = via_session.pixels[o + 1U], b = via_session.pixels[o + 2U];
            if (r == g && g == b && r >= 0x38U && r <= 0xB0U) ++grey;
        }
        assert(grey > 0U);
        // Source normals ride along (smooth-group key for the lit render).
        assert(session->render_mesh.has_normal0());
        assert(!dmcresource::describe_session(session.get()).empty());
        const auto mesh_info = dmcresource::inspect_session(session.get(), dmcresource::InspectionTopic::Meshes);
        assert(dmcresource::count_inspection_nodes(mesh_info.root, dmcresource::InspectionKind::Object) == 1);
        assert(dmcresource::count_inspection_nodes(mesh_info.root, dmcresource::InspectionKind::Mesh) == 1);
        assert(!dmcresource::inspect_session(session.get(), dmcresource::InspectionTopic::Uv).empty());
        // This minimal MOD fixture has no complete hierarchy permutation;
        // the SCM fixture has a validated root relation.
        for (const auto& node : session->scene.nodes) assert(node.parent_authority == (bytes == &scm));
        const auto hierarchy_info = dmcresource::format_inspection_tree(
            dmcresource::inspect_session(session.get(), dmcresource::InspectionTopic::Hierarchy));
        assert(hierarchy_info.find(bytes == &scm ? "Parent: Root" : "Parent: Unconfirmed") != std::string::npos);

        auto gallery = dmcresource::open_uv_gallery(session.get());
        assert(gallery && dmcresource::session_child_count(gallery.get()) == 1);
        assert(gallery->uv_gallery->maps[0].texture_slot == (bytes == &scm ? 0U : 5U));
        auto uv = dmcresource::open_session_child(gallery.get(), 0);
        assert(uv && widow::has_state(dmcresource::black_widow_state(uv.get()),
                                      widow::StateFlag::UvMapView));
        assert(dmcresource::render_session(uv.get(),128,128,0,0,1,0).pixels.size() == 128U*128U*4U);

    }

    const std::uint8_t old_family[] = {'H', 'I', 'T', 'S'};
    const auto rejected = run_decode_pipeline(
        "sample.hits", old_family, sizeof(old_family));
    assert(!rejected.accepted);

    return 0;
}
