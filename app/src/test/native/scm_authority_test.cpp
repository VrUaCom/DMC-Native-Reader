#include "dmcresource/decode_pipeline.h"
#include "dmcresource/scene_projection.h"

#include "dmc_rengine/formats/scm.hpp"
#include "dmc_rengine/formats/scm_layout.hpp"
#include "dmc_rengine/formats/scm_render.hpp"
#include "dmc_rengine/formats/scm_runtime_flags.hpp"

#include <bit>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
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
    for (std::size_t i = 0U; i < 2U; ++i) {
        put_u8(bytes, offset + i,
               static_cast<std::uint8_t>((value >> (i * 8U)) & 0xFFU));
    }
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

[[nodiscard]] bool near(float actual, float expected,
                        float epsilon = 0.0001F) noexcept {
    return std::fabs(actual - expected) <= epsilon;
}

std::vector<std::uint8_t> make_header_authority_scm(float version,
                                                    std::uint32_t resource_code) {
    // 0 objects, 2 helper scene nodes. This keeps the fixture small while
    // exercising the real +0x13 lighting-reference scene-node domain.
    std::vector<std::uint8_t> bytes(0xB0U, 0U);
    bytes[0] = 'S';
    bytes[1] = 'C';
    bytes[2] = 'M';
    bytes[3] = ' ';
    put_f32(bytes, 0x04U, version);
    put_u8(bytes, 0x10U, 0U);   // object_count
    put_u8(bytes, 0x11U, 2U);   // scene_node_count
    put_u8(bytes, 0x12U, 0U);   // texture_slot_count
    put_u8(bytes, 0x13U, 1U);   // lighting_reference_node_index
    put_u32(bytes, 0x14U, resource_code);
    put_u64(bytes, 0x20U, 0x40U);

    // Canonical scene layout for N=2:
    // parent_rel=0x20, order_rel=0x24, binding_rel=0x28,
    // transform_rel=0x30, EOF=0xB0.
    put_u32(bytes, 0x40U, 0x20U);
    put_u32(bytes, 0x44U, 0x24U);
    put_u32(bytes, 0x48U, 0x28U);
    put_u32(bytes, 0x4CU, 0x30U);

    bytes[0x60U] = 0xFFU; // root parent = -1
    bytes[0x61U] = 0x00U; // node 1 parent = node 0
    bytes[0x64U] = 0x00U;
    bytes[0x65U] = 0x01U;
    bytes[0x68U] = 0xFFU;
    bytes[0x69U] = 0xFFU;
    // Two 0x20-byte zero transforms begin at 0x70; zero translation has zero
    // magnitude and therefore passes the canonical transform invariant.
    return bytes;
}

std::vector<std::uint8_t> make_bound_spatial_scm() {
    namespace scm = dmc::rengine::formats::scm;

    scm::ObjectShape shape;
    shape.mesh_vertex_counts = {3U};
    const std::vector<scm::ObjectShape> shapes{shape};
    const auto layout = scm::build_serialized_layout(
        std::span<const scm::ObjectShape>{shapes}, 2U);
    std::vector<std::uint8_t> bytes(
        static_cast<std::size_t>(layout.file_size), 0U);

    bytes[0] = 'S';
    bytes[1] = 'C';
    bytes[2] = 'M';
    bytes[3] = ' ';
    put_f32(bytes, 0x04U, 1.01F);
    put_u8(bytes, 0x10U, 1U);
    put_u8(bytes, 0x11U, 2U);
    put_u8(bytes, 0x12U, 1U);
    put_u8(bytes, 0x13U, 0U);
    put_u32(bytes, 0x14U, 813800U);
    put_u64(bytes, 0x20U, layout.scene.block_offset);

    const auto& object_layout = layout.objects[0];
    const auto object_offset =
        static_cast<std::size_t>(object_layout.record_offset);
    put_u8(bytes, object_offset + 0x00U, 1U);
    put_u8(bytes, object_offset + 0x01U, 0x80U);
    put_u16(bytes, object_offset + 0x02U, 3U);
    put_u64(bytes, object_offset + 0x08U,
            object_layout.mesh_table_offset);

    const auto& mesh_layout = object_layout.meshes[0];
    const auto mesh_offset =
        static_cast<std::size_t>(mesh_layout.record_offset);
    put_u16(bytes, mesh_offset + 0x00U, 3U);
    put_u16(bytes, mesh_offset + 0x02U, 0U);
    put_u64(bytes, mesh_offset + 0x10U, mesh_layout.positions_offset);
    put_u64(bytes, mesh_offset + 0x18U, mesh_layout.normals_offset);
    put_u64(bytes, mesh_offset + 0x20U, mesh_layout.uv_offset);
    put_u64(bytes, mesh_offset + 0x28U, 0U);
    put_u64(bytes, mesh_offset + 0x38U, mesh_layout.color_flags_offset);
    put_u64(bytes, mesh_offset + 0x40U,
            mesh_layout.index_workspace_offset - mesh_layout.record_offset);
    put_u16(bytes,
            static_cast<std::size_t>(mesh_layout.index_workspace_offset),
            scm::index_workspace_sentinel);

    const auto positions =
        static_cast<std::size_t>(mesh_layout.positions_offset);
    put_f32(bytes, positions + 0x00U, 1.0F);
    put_f32(bytes, positions + 0x10U, 1.0F);
    put_f32(bytes, positions + 0x20U, 1.0F);

    const auto scene_offset =
        static_cast<std::size_t>(layout.scene.block_offset);
    put_u32(bytes, scene_offset + 0x00U, layout.scene.parent_rel);
    put_u32(bytes, scene_offset + 0x04U, layout.scene.order_rel);
    put_u32(bytes, scene_offset + 0x08U, layout.scene.object_binding_rel);
    put_u32(bytes, scene_offset + 0x0CU, layout.scene.transform_rel);

    // Evaluation order: root node 0, child node 1. Object 0 binds to child 1.
    put_u8(bytes, scene_offset + layout.scene.parent_rel + 0U, 0xFFU);
    put_u8(bytes, scene_offset + layout.scene.parent_rel + 1U, 0U);
    put_u8(bytes, scene_offset + layout.scene.order_rel + 0U, 0U);
    put_u8(bytes, scene_offset + layout.scene.order_rel + 1U, 1U);
    put_u8(bytes, scene_offset + layout.scene.object_binding_rel + 0U, 0xFFU);
    put_u8(bytes, scene_offset + layout.scene.object_binding_rel + 1U, 0U);

    // Root: +90 degrees Z, translation (10,20,30). Child: (+2,0,0).
    // Canonical DMC3 row-vector composition makes child world origin
    // (10,22,30), proving we did not merely add local translations.
    const auto root_transform = scene_offset + layout.scene.transform_rel;
    put_f32(bytes, root_transform + 0x00U, 10.0F);
    put_f32(bytes, root_transform + 0x04U, 20.0F);
    put_f32(bytes, root_transform + 0x08U, 30.0F);
    put_f32(bytes, root_transform + 0x0CU, std::sqrt(1400.0F));
    put_f32(bytes, root_transform + 0x18U, 1.5707963267948966F);

    const auto child_transform = root_transform + 0x20U;
    put_f32(bytes, child_transform + 0x00U, 2.0F);
    put_f32(bytes, child_transform + 0x0CU, 2.0F);

    return bytes;
}

}  // namespace

int main() {
    namespace scm = dmc::rengine::formats::scm;
    namespace runtime = dmc::rengine::formats::scm::runtime;

    static_assert(scm::is_confirmed_retail_version(0.83F));
    static_assert(scm::is_confirmed_retail_version(0.90F));
    static_assert(scm::is_confirmed_retail_version(1.00F));
    static_assert(scm::is_confirmed_retail_version(1.01F));
    static_assert(!scm::is_confirmed_retail_version(0.82F));

    for (const float version : {0.83F, 0.90F, 1.00F, 1.01F}) {
        const auto bytes = make_header_authority_scm(version, 813800U);
        const auto parsed = scm::Parser::parse(std::span<const std::byte>{
            reinterpret_cast<const std::byte*>(bytes.data()), bytes.size()});
        assert(parsed.ok());
        assert(parsed.document.header.version == version);
        assert(parsed.document.header.scene_node_count == 2U);
        assert(parsed.document.header.lighting_reference_node_index == 1U);
        assert(parsed.document.header.resource_code.raw == 813800U);
        assert(parsed.document.header.resource_code.family_class == 8U);
        assert(parsed.document.header.resource_code.model_set == 138U);
        assert(parsed.document.header.resource_code.sub_index == 0U);

        const auto pipeline = dmcresource::run_decode_pipeline(
            "authority.scm", bytes.data(), bytes.size());
        assert(pipeline.accepted);
        assert(pipeline.inspection.format == "SCM");
        assert(pipeline.scene.nodes.size() == 2U);
    }

    // End-to-end placement authority used by retail stage SCMs such as st002:
    // serialized hierarchy/order/binding -> world matrices -> primitive binding
    // -> final world-space render vertices.
    {
        const auto bytes = make_bound_spatial_scm();
        const auto pipeline = dmcresource::run_decode_pipeline(
            "st002-spatial-contract.scm", bytes.data(), bytes.size());
        assert(pipeline.accepted);
        assert(pipeline.scene.nodes.size() == 2U);
        assert(pipeline.scene.meshes.size() == 1U);
        assert(pipeline.scene.meshes[0].object_index == 0U);
        assert(pipeline.scene.meshes[0].node_index == 1);
        assert(pipeline.scene.nodes[1].parent == 0);
        assert(pipeline.scene.nodes[1].parent_authority);
        assert(pipeline.scene.nodes[1].spatial_authority);
        assert(near(pipeline.scene.nodes[1].world.values[12], 10.0F));
        assert(near(pipeline.scene.nodes[1].world.values[13], 22.0F));
        assert(near(pipeline.scene.nodes[1].world.values[14], 30.0F));

        dmcresource::Mesh materialized;
        assert(dmcresource::materialize_render_scene(
            pipeline.scene, &materialized));
        assert(materialized.vertices.size() == 3U);
        assert(materialized.indices.size() == 3U);
        assert(near(materialized.vertices[0].x, 10.0F));
        assert(near(materialized.vertices[0].y, 23.0F));
        assert(near(materialized.vertices[0].z, 30.0F));
        assert(near(materialized.vertices[1].x, 9.0F));
        assert(near(materialized.vertices[1].y, 22.0F));
        assert(near(materialized.vertices[1].z, 30.0F));
    }

    const auto family7 = scm::decode_legacy_resource_code(730507U);
    assert(family7.family_class == 7U);
    assert(family7.model_set == 305U);
    assert(family7.sub_index == 7U);
    assert(scm::matches_observed_scm_resource_code_shape(family7));

    assert(runtime::scm_compatibility_object_selector(0U) == 3U);
    assert(runtime::scm_compatibility_object_selector(
               runtime::source_mask_00080000) == 2U);
    assert(runtime::scm_compatibility_vs_base_key(2U) == 13U);
    assert(runtime::scm_compatibility_vs_base_key(3U) == 13U);

    assert(scm::scm_material_gif_tag_qword == 0x4000000000008001ULL);
    assert(scm::scm_material_gif_regs_qword == 0x000000000020EEEEULL);
    assert(scm::scm_material_ad_registers[0] == scm::legacy_gs_reg_tex0_1);
    assert(scm::scm_material_ad_registers[1] == scm::legacy_gs_reg_tex1_1);
    assert(scm::scm_material_ad_registers[2] == scm::legacy_gs_reg_clamp_1);
    assert(scm::scm_material_ad_registers[3] == scm::legacy_gs_reg_miptbp1_1);

    return 0;
}
