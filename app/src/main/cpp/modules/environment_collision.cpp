#include "dmcresource/environment_collision.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>

#include "dmc_rengine/formats/hits.hpp"

namespace dmcresource::environment_collision {
namespace {

[[nodiscard]] Vec3 convert(const dmc::rengine::formats::hits::Vec3& value) noexcept {
    return {value.x, value.y, value.z};
}

[[nodiscard]] std::uint32_t read_u32_le(std::span<const std::uint8_t> bytes,
                                        std::size_t offset) noexcept {
    if (offset > bytes.size() || bytes.size() - offset < 4U) return 0U;
    return static_cast<std::uint32_t>(bytes[offset]) |
           (static_cast<std::uint32_t>(bytes[offset + 1U]) << 8U) |
           (static_cast<std::uint32_t>(bytes[offset + 2U]) << 16U) |
           (static_cast<std::uint32_t>(bytes[offset + 3U]) << 24U);
}

}  // namespace

bool looks_like(std::span<const std::uint8_t> bytes) noexcept {
    return bytes.size() >= 4U && bytes[0] == static_cast<std::uint8_t>('H') &&
           bytes[1] == static_cast<std::uint8_t>('I') &&
           bytes[2] == static_cast<std::uint8_t>('T') &&
           bytes[3] == static_cast<std::uint8_t>('S');
}

std::optional<Source> parse(std::string_view resource_name,
                            std::uint32_t resource_slot,
                            std::span<const std::uint8_t> bytes) noexcept {
    try {
        const auto raw = std::span<const std::byte>{
            reinterpret_cast<const std::byte*>(bytes.data()), bytes.size()};
        const auto scan = dmc::rengine::formats::hits::RecordScanner::scan(raw);
        if (!scan.ok()) return std::nullopt;

        Source out;
        out.resource_name = std::string{resource_name};
        out.resource_slot = resource_slot;
        out.bounds_min = convert(scan.header.bounds_min);
        out.bounds_max = convert(scan.header.bounds_max);
        out.cell_size_raw = {
            read_u32_le(bytes, 0x20U),
            read_u32_le(bytes, 0x24U),
            read_u32_le(bytes, 0x28U),
        };
        out.grid_count_x = scan.header.grid_count_x;
        out.grid_count_y = scan.header.grid_count_y;
        out.grid_count_z = scan.header.grid_count_z;
        for (const auto& cell : scan.cells) {
            out.cell_reference_count += cell.triangle_byte_offsets.size();
        }
        out.triangles.reserve(scan.triangles.size());
        for (const auto& triangle : scan.triangles) {
            out.triangles.push_back({
                .flags = triangle.flags,
                .point_a = convert(triangle.point_a),
                .point_b = convert(triangle.point_b),
                .point_c = convert(triangle.point_c),
                .normal = convert(triangle.normal),
                .plane_d = triangle.plane_d,
            });
        }
        return out;
    } catch (...) {
        return std::nullopt;
    }
}

std::vector<Vec3> debug_lines(const Source& source) {
    std::vector<Vec3> out;
    out.reserve(source.triangles.size() * 6U);
    for (const auto& triangle : source.triangles) {
        out.push_back(triangle.point_a);
        out.push_back(triangle.point_b);
        out.push_back(triangle.point_b);
        out.push_back(triangle.point_c);
        out.push_back(triangle.point_c);
        out.push_back(triangle.point_a);
    }
    return out;
}

}  // namespace dmcresource::environment_collision
