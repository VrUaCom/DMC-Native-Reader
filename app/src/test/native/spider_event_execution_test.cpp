#include <cassert>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

#include "dmcresource/decode_pipeline.h"

namespace {

void put_u32(std::vector<std::uint8_t>& bytes,
             std::size_t offset,
             std::uint32_t value) {
    assert(offset + 4U <= bytes.size());
    for (std::size_t index = 0U; index < 4U; ++index) {
        bytes[offset + index] = static_cast<std::uint8_t>(
            (value >> (index * 8U)) & 0xFFU);
    }
}

std::vector<std::uint8_t> make_event_table() {
    std::vector<std::uint8_t> bytes(0x40U, 0U);
    bytes[0] = 'E';
    bytes[1] = 'V';
    bytes[2] = 'T';
    bytes[3] = 0U;
    put_u32(bytes, 0x04U, 0x00010001U);
    put_u32(bytes, 0x20U, 0x00000102U);
    put_u32(bytes, 0x24U, 0x37U);
    put_u32(bytes, 0x28U, 0x00000001U);
    put_u32(bytes, 0x2CU, 0x00000020U);
    put_u32(bytes, 0x08U, 0x2CU);
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
    const auto bytes = make_event_table();
    const auto result = dmcresource::run_decode_pipeline(
        "EventTbl00.bin", bytes.data(), bytes.size());

    assert(result.accepted);
    assert(!result.renderable);
    assert(result.inspection.format == "EventTbl");
    assert(trace_contains(result, "formats.evt.structural-reader"));
    assert(trace_contains(result, "spider.crusader"));
    return 0;
}
