#include "dmcresource/decode_pipeline.h"
#include "dmcresource/formats/dds.h"
#include "dmcresource/native_module.h"
#include "dmcresource/resource_capabilities.h"

#include <array>
#include <bit>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <string_view>
#include <vector>

static_assert(__cplusplus >= 202002L,
              "Native Reader architecture v2 requires C++20");

namespace {

void put_u8(std::vector<std::uint8_t>& bytes, std::size_t offset,
            std::uint8_t value) {
    assert(offset < bytes.size());
    bytes[offset] = value;
}

void put_u16(std::vector<std::uint8_t>& bytes, std::size_t offset,
             std::uint16_t value) {
    assert(offset + 2u <= bytes.size());
    put_u8(bytes, offset + 0u, static_cast<std::uint8_t>(value & 0xffu));
    put_u8(bytes, offset + 1u, static_cast<std::uint8_t>((value >> 8u) & 0xffu));
}

void put_u32(std::vector<std::uint8_t>& bytes, std::size_t offset,
             std::uint32_t value) {
    assert(offset + 4u <= bytes.size());
    bytes[offset + 0u] = static_cast<std::uint8_t>(value & 0xffu);
    bytes[offset + 1u] = static_cast<std::uint8_t>((value >> 8u) & 0xffu);
    bytes[offset + 2u] = static_cast<std::uint8_t>((value >> 16u) & 0xffu);
    bytes[offset + 3u] = static_cast<std::uint8_t>((value >> 24u) & 0xffu);
}

void put_u64(std::vector<std::uint8_t>& bytes, std::size_t offset,
             std::uint64_t value) {
    assert(offset + 8u <= bytes.size());
    for (std::size_t i = 0u; i < 8u; ++i) {
        put_u8(bytes, offset + i,
               static_cast<std::uint8_t>((value >> (i * 8u)) & 0xffu));
    }
}

void put_f32(std::vector<std::uint8_t>& bytes, std::size_t offset,
             float value) {
    put_u32(bytes, offset, std::bit_cast<std::uint32_t>(value));
}

std::vector<std::uint8_t> make_dds() {
    std::vector<std::uint8_t> bytes(128u + 24u, 0u);
    bytes[0] = 'D'; bytes[1] = 'D'; bytes[2] = 'S'; bytes[3] = ' ';
    put_u32(bytes, 4u, 124u);
    put_u32(bytes, 12u, 4u);
    put_u32(bytes, 16u, 4u);
    put_u32(bytes, 28u, 3u);
    put_u32(bytes, 76u, 32u);
    bytes[84] = 'D'; bytes[85] = 'X'; bytes[86] = 'T'; bytes[87] = '1';
    return bytes;
}

std::vector<std::uint8_t> make_ptx() {
    const auto dds = make_dds();
    std::vector<std::uint8_t> bytes(0x800u + 0x70u + dds.size(), 0u);
    put_u32(bytes, 0u, 1u);
    put_u32(bytes, 4u, 0u);
    put_u32(bytes, 0x800u + 0x38u, 24u);
    put_u32(bytes, 0x800u + 0x64u, static_cast<std::uint32_t>(dds.size()));
    std::memcpy(bytes.data() + 0x800u + 0x70u, dds.data(), dds.size());
    return bytes;
}

std::vector<std::uint8_t> make_mod() {
    std::vector<std::uint8_t> bytes(0x240u, 0u);
    bytes[0] = 'M'; bytes[1] = 'O'; bytes[2] = 'D'; bytes[3] = ' ';
    put_f32(bytes, 0x04u, 1.01f);
    put_u8(bytes, 0x10u, 1u);  // one object / outer model
    put_u8(bytes, 0x11u, 1u);  // one transform/skin-domain node
    put_u64(bytes, 0x20u, 0x200u);

    put_u8(bytes, 0x40u, 1u);  // one inner mesh
    put_u16(bytes, 0x42u, 3u); // aggregate vertices
    put_u64(bytes, 0x48u, 0x80u);

    put_u16(bytes, 0x80u, 3u);
    put_u64(bytes, 0x90u, 0xD0u);  // positions
    put_u64(bytes, 0x98u, 0x100u); // normals
    put_u64(bytes, 0xA0u, 0x130u); // UV
    put_u64(bytes, 0xA8u, 0x140u); // blend indices
    put_u64(bytes, 0xB0u, 0x150u); // packed weights/topology
    put_u64(bytes, 0xB8u, 0u);
    put_u64(bytes, 0xC0u, 0xE0u);  // record-relative -> 0x160 workspace
    put_u32(bytes, 0xC8u, 0u);
    put_u32(bytes, 0xCCu, 0u);

    // Triangle: (0,0,0), (1,0,0), (0,1,0).
    put_f32(bytes, 0xD0u, 0.0f); put_f32(bytes, 0xD4u, 0.0f); put_f32(bytes, 0xD8u, 0.0f);
    put_f32(bytes, 0xDCu, 1.0f); put_f32(bytes, 0xE0u, 0.0f); put_f32(bytes, 0xE4u, 0.0f);
    put_f32(bytes, 0xE8u, 0.0f); put_f32(bytes, 0xECu, 1.0f); put_f32(bytes, 0xF0u, 0.0f);

    for (std::size_t i = 0u; i < 3u; ++i) {
        const auto n = 0x100u + i * 12u;
        put_f32(bytes, n + 0u, 0.0f);
        put_f32(bytes, n + 4u, 0.0f);
        put_f32(bytes, n + 8u, 1.0f);
    }

    put_u16(bytes, 0x130u, 0u);    put_u16(bytes, 0x132u, 0u);
    put_u16(bytes, 0x134u, 4096u); put_u16(bytes, 0x136u, 0u);
    put_u16(bytes, 0x138u, 0u);    put_u16(bytes, 0x13Au, 4096u);

    // Blend-index lanes remain zero: active matrix row 0 -> bone 0.
    // Each control word carries a single 31/31 influence and no topology break.
    put_u16(bytes, 0x150u, 0x001Fu);
    put_u16(bytes, 0x152u, 0x001Fu);
    put_u16(bytes, 0x154u, 0x001Fu);

    // Transform-domain tables, offsets relative to 0x200.
    put_u32(bytes, 0x200u, 0x10u);
    put_u32(bytes, 0x204u, 0x20u);
    put_u32(bytes, 0x208u, 0x30u);
    put_u8(bytes, 0x210u, 0xFFu); // root
    put_u8(bytes, 0x220u, 0u);    // complete permutation
    put_u8(bytes, 0x230u, 0u);    // preserved third table
    return bytes;
}

const dmcresource::NativeModule& require_module(std::string_view family,
                                                dmcresource::Format format) {
    const auto* module = dmcresource::NativeModuleRegistry::find(family);
    assert(module != nullptr);
    assert(module->run != nullptr);
    assert(module->format == format);
    return *module;
}

void require_recognition(std::string_view family) {
    const auto* module = dmcresource::NativeModuleRegistry::find(family);
    assert(module != nullptr);
    assert(module->run != nullptr);
    assert(module->format == dmcresource::Format::Other);
    assert(module->kind == dmcresource::ModuleKind::Recognition);
    assert(!module->renderable);
}

bool module_trace_contains(const dmcresource::PipelineResult& result,
                           std::string_view id) {
    for (const auto& module : result.modules) {
        if (module.name != nullptr && std::string_view{module.name} == id && module.complete) {
            return true;
        }
    }
    return false;
}

}  // namespace

int main() {
    using dmcresource::Format;
    using dmcresource::ResourceCapability;
    using dmcresource::has_capability;
    using dmcresource::run_decode_pipeline;

    const auto& scm_module = require_module("SCM", Format::Scm);
    const auto& mod_module = require_module("MOD", Format::Mod);
    const auto& hits_module = require_module("HITS", Format::Hits);
    const auto& txt_module = require_module("TXT", Format::StageTxt);
    require_module(".index", Format::Index);
    const auto& dds_module = require_module("DDS", Format::Dds);
    const auto& ptx_module = require_module("PTX", Format::Ptx);
    require_module("DCA", Format::Dca);
    require_module("LIG", Format::Lig);
    require_module("LIG2", Format::Lig2);
    require_module("PAC", Format::Pac);
    require_module("PNST", Format::Pnst);
    require_module("NBZ", Format::Nbz);
    require_module("EFM", Format::Efm);
    require_module("MRP", Format::Mrp);
    require_module("SHW", Format::Shw);

    assert(has_capability(scm_module.capabilities, ResourceCapability::Inspection));
    assert(has_capability(scm_module.capabilities, ResourceCapability::Geometry));
    assert(has_capability(scm_module.capabilities, ResourceCapability::NodeHierarchy));
    assert(has_capability(scm_module.capabilities, ResourceCapability::TextureBinding));
    assert(!has_capability(scm_module.capabilities, ResourceCapability::SkeletalSkinning));

    assert(has_capability(mod_module.capabilities, ResourceCapability::Inspection));
    assert(has_capability(mod_module.capabilities, ResourceCapability::Geometry));
    assert(has_capability(mod_module.capabilities, ResourceCapability::NodeHierarchy));
    assert(has_capability(mod_module.capabilities, ResourceCapability::SkeletalSkinning));
    assert(has_capability(mod_module.capabilities, ResourceCapability::SkinWeights));
    assert(!has_capability(mod_module.capabilities, ResourceCapability::TextureBinding));

    assert(has_capability(hits_module.capabilities, ResourceCapability::Collision));
    assert(has_capability(hits_module.capabilities, ResourceCapability::Geometry));
    assert(has_capability(txt_module.capabilities, ResourceCapability::Text));
    assert(has_capability(dds_module.capabilities, ResourceCapability::Inspection));
    assert(has_capability(ptx_module.capabilities, ResourceCapability::Inspection));
    assert(has_capability(ptx_module.capabilities, ResourceCapability::ChildResources));

    constexpr std::array<std::string_view, 55> recognition_families{
        "PE", "PACK", ".lst", "AFS namespace", ".ukn", ".bin",
        "TIM2", "PTZ", "SEF", "EFE", "EFW", "C1D", "CLT",
        "MOT", "MOT2", "MOT3", "MOT4", "MOT5", "MOT6", "MCV",
        "CAM", "HID", "HID2", "HID3", "TSC", "EVE", "POS", "ITM",
        "STE", "EST", "ADX", "OGG", "VAGp", "PHD", "TSB", "BD",
        "SPUMAPDT", "SFD", "WMV", "PSS", "THP", "PAM", "XMV", "PMF",
        "AVI", "MPG", "BIK", "MP4", "SAV", "FON", "ICO", "icon.sys",
        "EventTbl", "options.sav", "dmc3.sav",
    };
    for (const auto family : recognition_families) require_recognition(family);

    assert(dmcresource::NativeModuleRegistry::modules().size() == 71u);
    assert(dmcresource::NativeModuleRegistry::find("UNMAPPED-FAMILY") == nullptr);

    const auto mod = make_mod();
    const auto mod_result = run_decode_pipeline("sample.mod", mod.data(), mod.size());
    assert(mod_result.accepted);
    assert(mod_result.renderable);
    assert(mod_result.probe.format == Format::Mod);
    assert(mod_result.detail.find("MOD canonical C++20 reader") != std::string::npos);
    assert(module_trace_contains(mod_result, "canonical.mod.structural-parser"));
    assert(mod_result.mesh.vertices.size() == 3u);
    assert(mod_result.mesh.indices.size() == 3u);
    assert(mod_result.mesh.indices[0] == 0u);
    assert(mod_result.mesh.indices[1] == 1u);
    assert(mod_result.mesh.indices[2] == 2u);
    assert(mod_result.scene.meshes.size() == 1u);
    assert(mod_result.scene.nodes.size() == 1u);
    assert(mod_result.scene.nodes[0].parent == -1);
    assert(mod_result.scene.skins.size() == 1u);
    assert(mod_result.scene.skins[0].vertices.size() == 3u);
    for (const auto& vertex : mod_result.scene.skins[0].vertices) {
        assert(vertex.influences.size() == 1u);
        assert(vertex.influences[0].node_index == 0u);
        assert(vertex.influences[0].weight > 0.999f);
    }
    assert(!mod_result.inspection.empty());
    assert(mod_result.inspection.format == "MOD");
    assert(mod_result.inspection.root.children.size() >= 2u);
    assert(has_capability(mod_result.capabilities, ResourceCapability::SkinWeights));
    assert(!has_capability(mod_result.capabilities, ResourceCapability::TextureBinding));

    const auto dds = make_dds();
    const auto raw_dds = dmcresource::formats::dds::parse(
        std::span<const std::uint8_t>{dds.data(), dds.size()});
    assert(raw_dds.ok);
    assert(raw_dds.document.width == 4u);
    assert(raw_dds.document.height == 4u);
    assert(raw_dds.document.mip_count == 3u);
    assert(raw_dds.document.total_size == dds.size());

    const auto dds_result = run_decode_pipeline("sample.dds", dds.data(), dds.size());
    assert(dds_result.accepted);
    assert(!dds_result.renderable);
    assert(dds_result.probe.format == Format::Dds);
    assert(has_capability(dds_result.capabilities, ResourceCapability::Inspection));
    assert(!dds_result.inspection.empty());
    assert(dds_result.inspection.format == "DDS");
    assert(dds_result.inspection.root.properties.size() >= 5u);

    const auto ptx = make_ptx();
    const auto ptx_result = run_decode_pipeline("sample.ptx", ptx.data(), ptx.size());
    assert(ptx_result.accepted);
    assert(ptx_result.probe.format == Format::Ptx);
    assert(ptx_result.detail.find("textures=1") != std::string::npos);
    assert(has_capability(ptx_result.capabilities, ResourceCapability::ChildResources));
    assert(!ptx_result.inspection.empty());
    assert(ptx_result.inspection.root.children.size() == 1u);
    assert(ptx_result.inspection.root.children[0].children.size() == 1u);

    std::vector<std::uint8_t> dca(0x10u + 0x410u, 0u);
    dca[0] = 'D'; dca[1] = 'C'; dca[2] = 'A'; dca[3] = 0;
    const auto dca_ok = run_decode_pipeline("sample.dca", dca.data(), dca.size());
    assert(dca_ok.accepted);
    assert(dca_ok.probe.format == Format::Dca);
    dca.pop_back();
    assert(!run_decode_pipeline("sample.dca", dca.data(), dca.size()).accepted);

    std::vector<std::uint8_t> lig2(0x20u + 0x30u, 0u);
    const auto lig2_result = run_decode_pipeline("sample.lig2", lig2.data(), lig2.size());
    assert(lig2_result.accepted);
    assert(lig2_result.probe.format == Format::Lig2);

    const char stage_text[] = "#SET DUMMY\nDOOR 1\n";
    const auto txt_result = run_decode_pipeline(
        "stage.txt", reinterpret_cast<const std::uint8_t*>(stage_text),
        sizeof(stage_text) - 1u);
    assert(txt_result.accepted);
    assert(txt_result.probe.format == Format::StageTxt);
    assert(has_capability(txt_result.capabilities, ResourceCapability::Text));

    const char index_text[] = "PNST\nfoo.mod\ndummy\n";
    const auto index_result = run_decode_pipeline(
        "stage.index", reinterpret_cast<const std::uint8_t*>(index_text),
        sizeof(index_text) - 1u);
    assert(index_result.accepted);
    assert(index_result.probe.format == Format::Index);

    std::vector<std::uint8_t> pac(16u, 0u);
    pac[0] = 'P'; pac[1] = 'A'; pac[2] = 'C'; pac[3] = 0;
    put_u32(pac, 4u, 1u);
    const auto pac_result = run_decode_pipeline("a.pac", pac.data(), pac.size());
    assert(pac_result.accepted);
    assert(pac_result.probe.format == Format::Pac);

    const std::uint8_t nbz[] = {'P', 'K', 3, 4};
    const auto nbz_result = run_decode_pipeline("DMC3-0.nbz", nbz, sizeof(nbz));
    assert(nbz_result.accepted);
    assert(nbz_result.probe.format == Format::Nbz);

    const std::uint8_t unknown_payload[] = {1u, 2u, 3u, 4u};
    const auto mot_result = run_decode_pipeline(
        "sample.mot", unknown_payload, sizeof(unknown_payload));
    assert(mot_result.accepted);
    assert(mot_result.probe.format == Format::Other);
    assert(!mot_result.modules.empty());
    assert(std::string_view{mot_result.modules.back().name} == "formats.mot.recognition");
    assert(!mot_result.modules.back().complete);

    const auto unknown_result = run_decode_pipeline(
        "sample.unrecognized", unknown_payload, sizeof(unknown_payload));
    assert(!unknown_result.accepted);

    return 0;
}
