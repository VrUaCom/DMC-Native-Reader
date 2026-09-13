#include "dmcresource/decode_pipeline.h"

#include "dmc_rengine/formats/scm.hpp"
#include "dmc_rengine/formats/scm_render.hpp"
#include "dmc_rengine/formats/scm_runtime_flags.hpp"

#include <bit>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace {

void put_u8(std::vector<std::uint8_t>& bytes, std::size_t offset,
            std::uint8_t value) {
    assert(offset < bytes.size());
    bytes[offset] = value;
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
