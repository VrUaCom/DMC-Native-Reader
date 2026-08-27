#include "dmcresource/decode.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>
#include <vector>

#include "dmcresource/binary_reader.h"
#include "dmcresource/hits_decode.h"
#include "dmcresource/text_decode.h"

namespace dmcresource {
namespace {

constexpr std::size_t kMaxResourceBytes = 512u * 1024u * 1024u;
constexpr std::size_t kHeaderSize = 0x40;
constexpr std::size_t kObjectStride = 0x40;
constexpr std::size_t kMeshDefStride = 0x50;
constexpr std::size_t kMaxObjects = 255;
constexpr std::size_t kMaxMeshes = 4096;
constexpr std::size_t kMaxVertices = 2u * 1024u * 1024u;
constexpr std::size_t kMaxIndices = 12u * 1024u * 1024u;

struct ObjectRec {
    std::uint8_t mesh_count{};
    std::uint16_t num_vertices{};
    std::size_t mesh_offset{};
};

struct MeshRec {
    std::uint16_t vertex_count{};
    std::size_t positions{};
    std::size_t normals{};
    std::size_t uvs{};
    std::size_t aux0{};
    std::size_t aux1{};
    std::size_t colors{};
    std::size_t tri_cmd{};
};

struct BoneState {
    int parent{-1};
    int child_object{-1};
    Vec3 local{};
    Vec3 world{};
    bool world_ready{};
    bool visiting{};
};

[[nodiscard]] bool exact_magic(const BinaryReader& reader,
                               const char a, const char b,
                               const char c, const char d) noexcept {
    const auto* p = reader.ptr(0, 4);
    return p != nullptr && p[0] == static_cast<std::uint8_t>(a) &&
           p[1] == static_cast<std::uint8_t>(b) &&
           p[2] == static_cast<std::uint8_t>(c) &&
           p[3] == static_cast<std::uint8_t>(d);
}

[[nodiscard]] bool supported_version(Format format, float version) noexcept {
    if (!std::isfinite(version)) return false;
    const auto close = [version](float expected) {
        return std::fabs(version - expected) < 0.0005f;
    };
    if (format == Format::Scm) return close(1.01f);
    if (format == Format::Mod) return close(0.90f) || close(1.00f) || close(1.01f);
    return false;
}

[[nodiscard]] bool read_offset(const BinaryReader& reader,
                               std::size_t field,
                               std::size_t* out) noexcept {
    if (out == nullptr) return false;
    std::uint64_t value = 0;
    if (!reader.read_le(field, &value)) return false;
    if (value > static_cast<std::uint64_t>(reader.size())) return false;
    if (value > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) return false;
    *out = static_cast<std::size_t>(value);
    return true;
}

[[nodiscard]] bool read_object(const BinaryReader& reader,
                               std::size_t index,
                               ObjectRec* out) noexcept {
    if (out == nullptr || index >= kMaxObjects) return false;
    const std::size_t base = kHeaderSize + index * kObjectStride;
    if (!reader.range(base, kObjectStride)) return false;
    std::uint8_t mesh_count = 0;
    std::uint16_t num_vertices = 0;
    std::size_t mesh_offset = 0;
    if (!reader.read_le(base + 0x00, &mesh_count) ||
        !reader.read_le(base + 0x02, &num_vertices) ||
        !read_offset(reader, base + 0x08, &mesh_offset)) {
        return false;
    }
    if (mesh_count != 0 &&
        !reader.table(mesh_offset, static_cast<std::size_t>(mesh_count), kMeshDefStride)) {
        return false;
    }
    *out = {mesh_count, num_vertices, mesh_offset};
    return true;
}

[[nodiscard]] bool read_mesh(const BinaryReader& reader,
                             std::size_t base,
                             Format format,
                             MeshRec* out) noexcept {
    if (out == nullptr || !reader.range(base, kMeshDefStride)) return false;
    MeshRec rec{};
    if (!reader.read_le(base + 0x00, &rec.vertex_count) ||
        !read_offset(reader, base + 0x10, &rec.positions) ||
        !read_offset(reader, base + 0x18, &rec.normals) ||
        !read_offset(reader, base + 0x20, &rec.uvs) ||
        !read_offset(reader, base + 0x28, &rec.aux0) ||
        !read_offset(reader, base + 0x30, &rec.aux1)) {
        return false;
    }
    if (format == Format::Scm) {
        if (!read_offset(reader, base + 0x38, &rec.colors) ||
            !read_offset(reader, base + 0x40, &rec.tri_cmd)) {
            return false;
        }
    }

    const auto vc = static_cast<std::size_t>(rec.vertex_count);
    if (vc == 0) {
        *out = rec;
        return true;
    }
    if (!reader.range(rec.positions, vc * 12u) ||
        !reader.range(rec.normals, vc * 12u) ||
        !reader.range(rec.uvs, vc * 4u)) {
        return false;
    }
    if (format == Format::Scm) {
        if (!reader.range(rec.colors, vc * 4u)) return false;
        if (rec.tri_cmd == 0 || !reader.range(rec.tri_cmd, 1)) return false;
    } else {
        if (!reader.range(rec.aux0, vc * 4u) ||
            !reader.range(rec.aux1, vc * 2u)) {
            return false;
        }
    }
    *out = rec;
    return true;
}

[[nodiscard]] Vec3 normalize(Vec3 v) noexcept {
    const float len2 = v.x*v.x + v.y*v.y + v.z*v.z;
    if (!(len2 > 1e-12f) || !std::isfinite(len2)) return {};
    const float inv = 1.0f / std::sqrt(len2);
    return {v.x * inv, v.y * inv, v.z * inv};
}

[[nodiscard]] Vec3 sub(Vec3 a, Vec3 b) noexcept {
    return {a.x-b.x, a.y-b.y, a.z-b.z};
}

[[nodiscard]] Vec3 add(Vec3 a, Vec3 b) noexcept {
    return {a.x+b.x, a.y+b.y, a.z+b.z};
}

[[nodiscard]] Vec3 cross(Vec3 a, Vec3 b) noexcept {
    return {a.y*b.z-a.z*b.y, a.z*b.x-a.x*b.z, a.x*b.y-a.y*b.x};
}

[[nodiscard]] float dot(Vec3 a, Vec3 b) noexcept {
    return a.x*b.x + a.y*b.y + a.z*b.z;
}

[[nodiscard]] bool read_vec3(const BinaryReader& reader,
                             std::size_t base,
                             Vec3* out) noexcept {
    if (out == nullptr) return false;
    Vec3 v{};
    if (!reader.read_le(base + 0, &v.x) ||
        !reader.read_le(base + 4, &v.y) ||
        !reader.read_le(base + 8, &v.z)) {
        return false;
