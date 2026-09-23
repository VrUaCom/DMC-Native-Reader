#include <bit>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

#include "dmcresource/decode_pipeline.h"
#include "dmcresource/native_module.h"
#include "dmcresource/resource_capabilities.h"

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
    for (std::size_t index = 0U; index < 4U; ++index) {
        put_u8(bytes, offset + index,
               static_cast<std::uint8_t>((value >> (index * 8U)) & 0xFFU));
    }
}

void put_u64(std::vector<std::uint8_t>& bytes, std::size_t offset,
             std::uint64_t value) {
    assert(offset + 8U <= bytes.size());
    for (std::size_t index = 0U; index < 8U; ++index) {
        put_u8(bytes, offset + index,
               static_cast<std::uint8_t>((value >> (index * 8U)) & 0xFFU));
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

    for (std::size_t index = 0U; index < 3U; ++index) {
        const auto normal = 0x100U + index * 12U;
        put_f32(bytes, normal + 0U, 0.0F);
        put_f32(bytes, normal + 4U, 0.0F);
        put_f32(bytes, normal + 8U, 1.0F);
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

}  // namespace

int main() {
    using dmcresource::NativeModuleRegistry;
    using dmcresource::ResourceCapability;
    using dmcresource::has_capability;

    const auto& modules = NativeModuleRegistry::modules();
    assert(modules.size() == 8U);

    const auto* mod = NativeModuleRegistry::find("MOD");
    const auto* scm = NativeModuleRegistry::find("SCM");
    const auto* dds = NativeModuleRegistry::find("DDS");
    const auto* ptx = NativeModuleRegistry::find("PTX");
    const auto* event_tbl = NativeModuleRegistry::find("EventTbl");
    assert(mod != nullptr && scm != nullptr && dds != nullptr && ptx != nullptr &&
           event_tbl != nullptr);

    // MOD and SCM share one Spider-backed model execution entry point; DDS and
    // PTX share the Spider-backed texture entry point; EventTbl owns a separate
    // Spider-backed structural route. This keeps all five promoted families on
    // Crusader without conflating their product domains.
    assert(mod->run == scm->run);
    assert(dds->run == ptx->run);
    assert(mod->run != dds->run);
    assert(event_tbl->run != nullptr);
    assert(event_tbl->run != mod->run);
    assert(event_tbl->run != dds->run);

    // Black Widow can distinguish the promoted skeletal MOD model family from
    // SCM without Android inspecting filenames: the distinction is carried by
    // typed module capabilities published by the registry.
    assert(has_capability(mod->capabilities, ResourceCapability::SkeletalSkinning));
    assert(!has_capability(scm->capabilities, ResourceCapability::SkeletalSkinning));

    const auto bytes = make_mod();
    const auto result = dmcresource::run_decode_pipeline(
        "sample.mod", bytes.data(), bytes.size());
    assert(result.accepted);
    assert(result.renderable);
    assert(trace_contains(result, "formats.mod.mesh-reader"));
    assert(trace_contains(result, "spider.crusader"));
    assert(trace_contains(result, "render-scene-contract"));

    return 0;
}
