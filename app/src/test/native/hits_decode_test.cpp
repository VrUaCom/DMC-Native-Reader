// Host test for the HITS collision decoder.
//
// The fixture layout mirrors `tests/hits_test_fixture.hpp` in dmc-rengine-cpp,
// so this checks the Android port against the same bytes the structural
// authority is tested with. Build and run:
//
//   g++ -std=c++17 -Wall -Wextra -Iapp/src/main/cpp/include
//       app/src/test/native/hits_decode_test.cpp
//       app/src/main/cpp/decode_hits.cpp -o /tmp/hits_test && /tmp/hits_test

#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

#include "dmcresource/hits_decode.h"

namespace {

int failures = 0;

void check(bool condition, const std::string& what) {
    if (condition) {
        std::cout << "  ok   " << what << "\n";
    } else {
        std::cout << "  FAIL " << what << "\n";
        ++failures;
    }
}

void write_u32(std::vector<std::uint8_t>& bytes, std::size_t offset,
               std::uint32_t value) {
    for (std::size_t i = 0; i < 4u; ++i) {
        bytes[offset + i] = static_cast<std::uint8_t>((value >> (i * 8u)) & 0xFFu);
    }
}

void write_f32(std::vector<std::uint8_t>& bytes, std::size_t offset, float value) {
    std::uint32_t raw = 0;
    std::memcpy(&raw, &value, 4u);
    write_u32(bytes, offset, raw);
}

constexpr std::size_t kSpatialOffset = 0x44;
constexpr std::size_t kListOffset = 0x48;
constexpr std::size_t kTriangleOffset = 0x50;
constexpr std::size_t kFileSize = 0x90;

std::vector<std::uint8_t> make_minimal_hits() {
    std::vector<std::uint8_t> bytes(kFileSize, 0u);
    bytes[0] = 'H';
    bytes[1] = 'I';
    bytes[2] = 'T';
    bytes[3] = 'S';

    write_u32(bytes, 0x04, static_cast<std::uint32_t>(kTriangleOffset + 0x38u));

    write_f32(bytes, 0x08, 0.0f);   // bounds_min
    write_f32(bytes, 0x0C, 0.0f);
    write_f32(bytes, 0x10, 0.0f);
    write_f32(bytes, 0x14, 10.0f);  // bounds_max
    write_f32(bytes, 0x18, 10.0f);
    write_f32(bytes, 0x1C, 10.0f);
    write_f32(bytes, 0x20, 10.0f);  // cell_size
    write_f32(bytes, 0x24, 10.0f);
    write_f32(bytes, 0x28, 10.0f);

    write_u32(bytes, 0x2C, 1u);     // grid x/y/z
    write_u32(bytes, 0x30, 1u);
    write_u32(bytes, 0x34, 1u);
    write_u32(bytes, 0x38, 1u);     // triangle_count
    write_u32(bytes, 0x3C, static_cast<std::uint32_t>(kSpatialOffset - 8u));
    write_u32(bytes, 0x40, static_cast<std::uint32_t>(kTriangleOffset - 8u));

    write_u32(bytes, kSpatialOffset, static_cast<std::uint32_t>(kListOffset - 8u));
    write_u32(bytes, kListOffset, 0u);
    write_u32(bytes, kListOffset + 4u, 0xFFFFFFFFu);  // -1 terminator

    write_u32(bytes, kTriangleOffset + 0x00, 0x00000001u);  // raw_flags
    write_f32(bytes, kTriangleOffset + 0x04, 0.0f);         // point_a
    write_f32(bytes, kTriangleOffset + 0x08, 0.0f);
    write_f32(bytes, kTriangleOffset + 0x0C, 0.0f);
    write_f32(bytes, kTriangleOffset + 0x10, 1.0f);         // point_b
    write_f32(bytes, kTriangleOffset + 0x14, 0.0f);
    write_f32(bytes, kTriangleOffset + 0x18, 0.0f);
    write_f32(bytes, kTriangleOffset + 0x1C, 0.0f);         // point_c
    write_f32(bytes, kTriangleOffset + 0x20, 0.0f);
    write_f32(bytes, kTriangleOffset + 0x24, 1.0f);
    write_f32(bytes, kTriangleOffset + 0x28, 0.0f);         // normal
    write_f32(bytes, kTriangleOffset + 0x2C, -1.0f);
    write_f32(bytes, kTriangleOffset + 0x30, 0.0f);
    write_f32(bytes, kTriangleOffset + 0x34, 0.0f);         // plane_d
    return bytes;
}

dmcresource::DecodeResult run(const std::vector<std::uint8_t>& bytes) {
    return dmcresource::decode_hits(bytes.data(), bytes.size());
}

bool has_note(const dmcresource::DecodeResult& result) {
    return result.info.find('[') != std::string::npos;
}

void test_minimal_fixture() {
    std::cout << "minimal authority fixture\n";
    const auto result = run(make_minimal_hits());
    check(result.status == dmcresource::DecodeStatus::Ok, "decodes");
    check(result.format == dmcresource::Format::Hits, "reports HITS");
    check(result.mesh.vertices.size() == 3u, "materializes 3 vertices");
    check(result.mesh.indices.size() == 3u, "materializes 1 triangle");
    check(result.info.find("grid=1x1x1") != std::string::npos, "reports grid");
    check(result.info.find("cells=1") != std::string::npos, "reports cell count");
    check(result.info.find("cellRefs=1") != std::string::npos, "walks the cell list");
    check(!has_note(result), "reports no structural notes");

    const auto& v = result.mesh.vertices;
    check(v[0].x == 0.0f && v[0].y == 0.0f && v[0].z == 0.0f, "point_a exact");
    check(v[1].x == 1.0f && v[1].y == 0.0f && v[1].z == 0.0f, "point_b exact");
    check(v[2].x == 0.0f && v[2].y == 0.0f && v[2].z == 1.0f, "point_c exact");
}

void test_rejects_foreign_magic() {
    std::cout << "foreign magic\n";
    auto bytes = make_minimal_hits();
    bytes[3] = '$';  // the superseded five-byte HITS$ reading must not match
    const auto result = run(bytes);
    check(result.status == dmcresource::DecodeStatus::UnknownFormat, "rejected");
    check(result.mesh.vertices.empty(), "yields no geometry");
}

void test_rejects_truncated_header() {
    std::cout << "truncated header\n";
    auto bytes = make_minimal_hits();
    bytes.resize(0x20);
    const auto result = run(bytes);
    check(result.status == dmcresource::DecodeStatus::InvalidData, "rejected");
}

void test_rejects_zero_grid() {
    std::cout << "zero grid dimension\n";
    auto bytes = make_minimal_hits();
    write_u32(bytes, 0x30, 0u);
    check(run(bytes).status == dmcresource::DecodeStatus::InvalidData, "rejected");
}

void test_rejects_nonpositive_cell_size() {
    std::cout << "non-positive cell size\n";
    auto bytes = make_minimal_hits();
    write_f32(bytes, 0x24, 0.0f);
    check(run(bytes).status == dmcresource::DecodeStatus::InvalidData, "rejected");
}

void test_rejects_triangle_array_overrun() {
    std::cout << "triangle array past end of file\n";
    auto bytes = make_minimal_hits();
    write_u32(bytes, 0x38, 64u);
    check(run(bytes).status == dmcresource::DecodeStatus::InvalidData, "rejected");
}

void test_rejects_no_triangles() {
    std::cout << "zero declared triangles\n";
    auto bytes = make_minimal_hits();
    write_u32(bytes, 0x38, 0u);
    check(run(bytes).status == dmcresource::DecodeStatus::InvalidData, "rejected");
}

void test_rejects_nonfinite_vertex() {
    std::cout << "non-finite vertex\n";
    auto bytes = make_minimal_hits();
    write_f32(bytes, kTriangleOffset + 0x04,
              std::numeric_limits<float>::quiet_NaN());
    check(run(bytes).status == dmcresource::DecodeStatus::InvalidData, "rejected");
}

void test_rejects_grid_overflow() {
    std::cout << "grid dimensions overflowing the cell count\n";
    auto bytes = make_minimal_hits();
    write_u32(bytes, 0x2C, 0xFFFFFFFFu);
    write_u32(bytes, 0x30, 0xFFFFFFFFu);
    write_u32(bytes, 0x34, 0xFFFFFFFFu);
    const auto result = run(bytes);
    // Geometry still decodes; the unusable grid is reported, not wrapped around.
    check(result.status == dmcresource::DecodeStatus::Ok, "geometry still decodes");
    check(has_note(result), "reports the unusable grid");
}

void test_notes_end_offset_mismatch() {
    std::cout << "end offset mismatch\n";
    auto bytes = make_minimal_hits();
    write_u32(bytes, 0x04, 0x10u);
    const auto result = run(bytes);
    check(result.status == dmcresource::DecodeStatus::Ok, "geometry still decodes");
    check(result.info.find("end offset") != std::string::npos, "reports the mismatch");
}

void test_notes_unterminated_cell_list() {
    std::cout << "unterminated cell list\n";
    auto bytes = make_minimal_hits();
    write_u32(bytes, kListOffset + 4u, 0u);  // valid reference instead of -1
    const auto result = run(bytes);
    // Deliberate deviation from the authority: a cell-table defect is reported
    // rather than allowed to hide triangles that decoded cleanly.
    check(result.status == dmcresource::DecodeStatus::Ok, "geometry still decodes");
    check(result.mesh.indices.size() == 3u, "triangle survives");
    check(has_note(result), "reports the defect");
}

void test_rejects_empty_input() {
    std::cout << "empty input\n";
    const auto result = dmcresource::decode_hits(nullptr, 0);
    check(result.status == dmcresource::DecodeStatus::UnknownFormat, "rejected");
}

}  // namespace

int main() {
    test_minimal_fixture();
    test_rejects_foreign_magic();
    test_rejects_truncated_header();
    test_rejects_zero_grid();
    test_rejects_nonpositive_cell_size();
    test_rejects_triangle_array_overrun();
    test_rejects_no_triangles();
    test_rejects_nonfinite_vertex();
    test_rejects_grid_overflow();
    test_notes_end_offset_mismatch();
    test_notes_unterminated_cell_list();
    test_rejects_empty_input();

    if (failures != 0) {
        std::cout << "\n" << failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "\nall HITS decoder checks passed\n";
    return 0;
}
