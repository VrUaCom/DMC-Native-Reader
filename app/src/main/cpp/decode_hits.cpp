#include "dmcresource/hits_decode.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>
#include <string>

#include "dmcresource/binary_reader.h"

namespace dmcresource {
namespace {

// Structural constants from the dmc-rengine-cpp HITS authority.
constexpr std::size_t kHitsHeaderSize = 0x44;
constexpr std::size_t kTriangleStride = 0x38;
constexpr std::uint64_t kRelativeOffsetBase = 8;
constexpr std::int32_t kCellListTerminator = -1;
constexpr float kPlaneResidualTolerance = 0.01f;

// Viewer-side caps. The authority parses on a desktop; this decoder runs on a
// phone against files the user may have obtained anywhere, so the cell walk
// needs a work budget as well as a size bound.
constexpr std::uint32_t kMaxTriangles = 4u * 1024u * 1024u;
constexpr std::uint64_t kMaxCellWalkReads = 32u * 1024u * 1024u;

[[nodiscard]] bool read_vec3(const BinaryReader& reader,
                             std::size_t offset,
                             Vec3* out) noexcept {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    if (!reader.read_le(offset, &x) ||
        !reader.read_le(offset + 4u, &y) ||
        !reader.read_le(offset + 8u, &z)) {
        return false;
    }
    *out = Vec3{x, y, z};
    return true;
}

[[nodiscard]] bool read_i32(const BinaryReader& reader,
                            std::uint64_t offset,
                            std::int32_t* out) noexcept {
    if (offset > std::numeric_limits<std::size_t>::max()) return false;
    std::uint32_t raw = 0;
    if (!reader.read_le(static_cast<std::size_t>(offset), &raw)) return false;
    *out = static_cast<std::int32_t>(raw);
    return true;
}

[[nodiscard]] bool finite3(const Vec3& v) noexcept {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

[[nodiscard]] float evaluate_plane(const Vec3& normal, float plane_d,
                                   const Vec3& point) noexcept {
    return normal.x * point.x + normal.y * point.y + normal.z * point.z + plane_d;
}

// grid_x * grid_y * grid_z can exceed 64 bits, so the product is built with an
// explicit overflow guard rather than allowed to wrap.
[[nodiscard]] bool checked_cell_count(std::uint32_t x, std::uint32_t y,
                                      std::uint32_t z,
                                      std::uint64_t* out) noexcept {
    constexpr std::uint64_t kMax = std::numeric_limits<std::uint64_t>::max();
    std::uint64_t total = x;
    if (y != 0 && total > kMax / y) return false;
    total *= y;
    if (z != 0 && total > kMax / z) return false;
    total *= z;
    *out = total;
    return true;
}

void append_note(std::string* notes, const char* note) {
    if (!notes->empty()) notes->append("; ");
    notes->append(note);
}

// Walks the spatial acceleration grid purely to report on it.
//
// Deliberate deviation from the authority: the reference scanner treats every
// cell-table defect as a hard error. This is a viewer, and by the time the walk
// runs the magic, the bounded header and the whole triangle array have already
// validated, so a defect here is reported rather than allowed to hide geometry
// that decodes cleanly.
void walk_cells(const BinaryReader& reader,
                std::uint64_t cell_count,
                std::uint64_t spatial_offset,
                std::uint64_t triangle_offset,
                std::uint64_t triangle_bytes,
                std::size_t size,
                std::uint64_t* referenced_out,
                std::string* notes) {
    std::uint64_t pointer_bytes = 0;
    if (cell_count > std::numeric_limits<std::uint64_t>::max() / 4u) {
        append_note(notes, "cell table too large to validate");
        return;
    }
    pointer_bytes = cell_count * 4u;
    if (spatial_offset > size || pointer_bytes > size - spatial_offset) {
        append_note(notes, "cell pointer table exceeds the resource");
        return;
    }

    std::uint64_t reads = 0;
    std::uint64_t referenced = 0;
    for (std::uint64_t index = 0; index < cell_count; ++index) {
        const std::uint64_t pointer_offset = spatial_offset + index * 4u;
        std::int32_t relative = 0;
        if (!read_i32(reader, pointer_offset, &relative) || relative < 0) {
            append_note(notes, "unreadable or negative cell pointer");
            return;
        }
        const std::uint64_t list_offset =
            kRelativeOffsetBase + static_cast<std::uint64_t>(relative);
        if (list_offset >= triangle_offset || list_offset >= size) {
            append_note(notes, "cell list pointer leaves the spatial section");
            return;
        }

        std::uint64_t cursor = list_offset;
        bool terminated = false;
        while (cursor + 4u <= triangle_offset) {
            if (++reads > kMaxCellWalkReads) {
                append_note(notes, "cell walk exceeded the decoder work budget");
                return;
            }
            std::int32_t reference = 0;
            if (!read_i32(reader, cursor, &reference)) break;
            cursor += 4u;
            if (reference == kCellListTerminator) {
                terminated = true;
                break;
            }
            if (reference < 0 ||
                (reference % static_cast<std::int32_t>(kTriangleStride)) != 0 ||
                static_cast<std::uint64_t>(reference) >= triangle_bytes) {
                append_note(notes, "cell entry is not a valid 0x38-byte triangle offset");
                return;
            }
            ++referenced;
        }
        if (!terminated) {
            append_note(notes, "cell list has no -1 terminator before the triangle array");
            return;
        }
    }
    *referenced_out = referenced;
}

}  // namespace

DecodeResult decode_hits(const std::uint8_t* bytes, std::size_t size) noexcept {
    try {
        const BinaryReader reader(bytes, size);

        const std::uint8_t* magic = reader.ptr(0, 4);
        if (magic == nullptr || magic[0] != 'H' || magic[1] != 'I' ||
            magic[2] != 'T' || magic[3] != 'S') {
            return {DecodeStatus::UnknownFormat, Format::Unknown, {},
                    "resource does not begin with the four-byte HITS magic", {}};
        }
        if (!reader.range(0, kHitsHeaderSize)) {
            return {DecodeStatus::InvalidData, Format::Hits, {},
                    "HITS header is shorter than 0x44 bytes", {}};
        }

        std::uint32_t end_offset = 0;
        std::uint32_t grid_x = 0;
        std::uint32_t grid_y = 0;
        std::uint32_t grid_z = 0;
        std::uint32_t triangle_count = 0;
        std::uint32_t spatial_relative = 0;
        std::uint32_t triangle_relative = 0;
        Vec3 bounds_min{};
        Vec3 bounds_max{};
        Vec3 cell_size{};
        if (!reader.read_le(0x04, &end_offset) ||
            !reader.read_le(0x2C, &grid_x) ||
            !reader.read_le(0x30, &grid_y) ||
            !reader.read_le(0x34, &grid_z) ||
            !reader.read_le(0x38, &triangle_count) ||
            !reader.read_le(0x3C, &spatial_relative) ||
            !reader.read_le(0x40, &triangle_relative) ||
            !read_vec3(reader, 0x08, &bounds_min) ||
            !read_vec3(reader, 0x14, &bounds_max) ||
            !read_vec3(reader, 0x20, &cell_size)) {
            return {DecodeStatus::InvalidData, Format::Hits, {},
                    "HITS header could not be decoded completely", {}};
        }

        if (grid_x == 0 || grid_y == 0 || grid_z == 0 ||
            !finite3(bounds_min) || !finite3(bounds_max) || !finite3(cell_size) ||
            cell_size.x <= 0.0f || cell_size.y <= 0.0f || cell_size.z <= 0.0f) {
            return {DecodeStatus::InvalidData, Format::Hits, {},
                    "HITS grid dimensions and cell sizes must be finite and non-zero", {}};
        }

        if (triangle_count > kMaxTriangles) {
            return {DecodeStatus::TooLarge, Format::Hits, {},
                    "HITS triangle count exceeds decoder cap", {}};
        }

        const std::uint64_t spatial_offset = kRelativeOffsetBase + spatial_relative;
        const std::uint64_t triangle_offset = kRelativeOffsetBase + triangle_relative;
        const std::uint64_t triangle_bytes =
            static_cast<std::uint64_t>(triangle_count) * kTriangleStride;
        if (spatial_offset > size || triangle_offset > size ||
            triangle_bytes > size - triangle_offset || end_offset > size) {
            return {DecodeStatus::InvalidData, Format::Hits, {},
                    "a HITS header offset, count or terminal array exceeds the resource", {}};
        }
        if (triangle_count == 0) {
            return {DecodeStatus::InvalidData, Format::Hits, {},
                    "HITS header is valid but declares no triangles", {}};
        }

        std::string notes;
        if (end_offset != triangle_offset + triangle_bytes) {
            append_note(&notes, "end offset != triangleBase + triangleCount * 0x38");
        }

        Mesh mesh;
        mesh.vertices.reserve(static_cast<std::size_t>(triangle_count) * 3u);
        mesh.indices.reserve(static_cast<std::size_t>(triangle_count) * 3u);

        std::uint32_t plane_residual_hits = 0;
        for (std::uint32_t index = 0; index < triangle_count; ++index) {
            const std::uint64_t offset64 =
                triangle_offset + static_cast<std::uint64_t>(index) * kTriangleStride;
            const std::size_t offset = static_cast<std::size_t>(offset64);

            std::uint32_t flags = 0;
            float plane_d = 0.0f;
            Vec3 a{};
            Vec3 b{};
            Vec3 c{};
            Vec3 normal{};
            if (!reader.read_le(offset, &flags) ||
                !reader.read_le(offset + 0x34u, &plane_d) ||
                !read_vec3(reader, offset + 0x04u, &a) ||
                !read_vec3(reader, offset + 0x10u, &b) ||
                !read_vec3(reader, offset + 0x1Cu, &c) ||
                !read_vec3(reader, offset + 0x28u, &normal)) {
                return {DecodeStatus::InvalidData, Format::Hits, {},
                        "a declared HITS triangle could not be decoded completely", {}};
            }
            (void)flags;  // raw_flags semantics remain evidence-gated.
            if (!finite3(a) || !finite3(b) || !finite3(c) || !finite3(normal) ||
                !std::isfinite(plane_d)) {
                return {DecodeStatus::InvalidData, Format::Hits, {},
                        "a HITS triangle contains a non-finite geometry value", {}};
            }

            const float residual_a = std::fabs(evaluate_plane(normal, plane_d, a));
            const float residual_b = std::fabs(evaluate_plane(normal, plane_d, b));
            const float residual_c = std::fabs(evaluate_plane(normal, plane_d, c));
            float residual = residual_a;
            if (residual_b > residual) residual = residual_b;
            if (residual_c > residual) residual = residual_c;
            if (residual > kPlaneResidualTolerance) ++plane_residual_hits;

            const auto base = static_cast<std::uint32_t>(mesh.vertices.size());
            mesh.vertices.push_back(a);
            mesh.vertices.push_back(b);
            mesh.vertices.push_back(c);
            mesh.indices.push_back(base);
            mesh.indices.push_back(base + 1u);
            mesh.indices.push_back(base + 2u);
        }

        std::uint64_t cell_count = 0;
        std::uint64_t referenced = 0;
        if (!checked_cell_count(grid_x, grid_y, grid_z, &cell_count)) {
            append_note(&notes, "grid dimensions overflow the cell count");
        } else {
            walk_cells(reader, cell_count, spatial_offset, triangle_offset,
                       triangle_bytes, size, &referenced, &notes);
        }

        std::string info = "grid=" + std::to_string(grid_x) + "x" +
                           std::to_string(grid_y) + "x" + std::to_string(grid_z) +
                           " cells=" + std::to_string(cell_count) +
                           " cellRefs=" + std::to_string(referenced);
        if (plane_residual_hits != 0) {
            info += " planeResidual=" + std::to_string(plane_residual_hits);
        }
        if (!notes.empty()) info += " [" + notes + "]";

        return {DecodeStatus::Ok, Format::Hits, std::move(mesh),
                "HITS collision grid decode", std::move(info)};
    } catch (const std::bad_alloc&) {
        return {DecodeStatus::TooLarge, Format::Hits, {},
                "decoder allocation failed", {}};
    } catch (...) {
        return {DecodeStatus::InvalidData, Format::Hits, {},
                "decoder rejected malformed input", {}};
    }
}

}  // namespace dmcresource
