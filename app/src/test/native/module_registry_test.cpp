#include "dmcresource/decode_pipeline.h"
#include "dmcresource/native_module.h"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>
#include <vector>

namespace {

void put_u32(std::vector<std::uint8_t>& bytes, std::size_t offset,
             std::uint32_t value) {
    assert(offset + 4u <= bytes.size());
    bytes[offset + 0u] = static_cast<std::uint8_t>(value & 0xffu);
    bytes[offset + 1u] = static_cast<std::uint8_t>((value >> 8u) & 0xffu);
    bytes[offset + 2u] = static_cast<std::uint8_t>((value >> 16u) & 0xffu);
    bytes[offset + 3u] = static_cast<std::uint8_t>((value >> 24u) & 0xffu);
}

std::vector<std::uint8_t> make_dds() {
    // 4x4 DXT1 full mip chain: 4x4, 2x2, 1x1 = 3 blocks = 24 bytes.
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
    put_u32(bytes, 0u, 1u);      // texture count
    put_u32(bytes, 4u, 0u);      // final zero span reaches exact EOF
    put_u32(bytes, 0x800u + 0x38u, 24u);
    put_u32(bytes, 0x800u + 0x64u, static_cast<std::uint32_t>(dds.size()));
    std::memcpy(bytes.data() + 0x800u + 0x70u, dds.data(), dds.size());
    return bytes;
}

void require_module(std::string_view family) {
    const auto* module = dmcresource::NativeModuleRegistry::find(family);
    assert(module != nullptr);
    assert(module->run != nullptr);
}

}  // namespace

int main() {
    using dmcresource::run_decode_pipeline;

    for (const auto family : {"SCM", "MOD", "HITS", "TXT", ".index",
                              "DDS", "PTX", "DCA", "LIG", "LIG2",
                              "PAC", "PNST", "NBZ", "EFM", "MRP", "SHW"}) {
        require_module(family);
    }
    require_module("UNMAPPED-FAMILY"); // generic fallback

    const auto dds = make_dds();
    const auto dds_result = run_decode_pipeline("sample.dds", dds.data(), dds.size());
    assert(dds_result.accepted);
    assert(!dds_result.renderable);

    const auto ptx = make_ptx();
    const auto ptx_result = run_decode_pipeline("sample.ptx", ptx.data(), ptx.size());
    assert(ptx_result.accepted);
    assert(ptx_result.detail.find("textures=1") != std::string::npos);

    std::vector<std::uint8_t> dca(0x10u + 0x410u, 0u);
    dca[0] = 'D'; dca[1] = 'C'; dca[2] = 'A'; dca[3] = 0;
    assert(run_decode_pipeline("sample.dca", dca.data(), dca.size()).accepted);
    dca.pop_back();
    assert(!run_decode_pipeline("sample.dca", dca.data(), dca.size()).accepted);

    std::vector<std::uint8_t> lig2(0x20u + 0x30u, 0u);
    assert(run_decode_pipeline("sample.lig2", lig2.data(), lig2.size()).accepted);

    const char stage_text[] = "#SET DUMMY\nDOOR 1\n";
    const auto txt_result = run_decode_pipeline(
        "stage.txt", reinterpret_cast<const std::uint8_t*>(stage_text),
        sizeof(stage_text) - 1u);
    assert(txt_result.accepted);

    const char index_text[] = "PNST\nfoo.mod\ndummy\n";
    const auto index_result = run_decode_pipeline(
        "stage.index", reinterpret_cast<const std::uint8_t*>(index_text),
        sizeof(index_text) - 1u);
    assert(index_result.accepted);

    std::vector<std::uint8_t> pac(16u, 0u);
    pac[0] = 'P'; pac[1] = 'A'; pac[2] = 'C'; pac[3] = 0;
    put_u32(pac, 4u, 1u);
    assert(run_decode_pipeline("a.pac", pac.data(), pac.size()).accepted);

    const std::uint8_t nbz[] = {'P', 'K', 3, 4};
    assert(run_decode_pipeline("DMC3-0.nbz", nbz, sizeof(nbz)).accepted);

    return 0;
}
