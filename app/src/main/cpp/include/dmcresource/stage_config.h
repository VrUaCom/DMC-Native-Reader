#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

// The members of a stage configuration archive (stNNNcfg.pac), read from
// their bytes. Ported from dmc-rengine-cpp formats/stage_cfg, whose readers
// were checked against every byte of the retail files.
//
// - POS: 16-byte header (POS\0, u16 version, u16 count), 48-byte records to
//   the end of the file: u32, position, heading in degrees, zeros. Records
//   past `count` are zero. What the game places with them: spawn positions.
// - EVE: 16-byte header, exactly `count` 128-byte records: three u32, at +0x20
//   four (x, y, z, 1) corners of a floor quadrilateral, at +0x60 the height
//   the volume reaches. Event zones.
// - CAM: 16-byte header, `count` records of varying length. Records that open
//   with flags 0x400 carry a zone count at +0x14 and are 0x40 + count * 0x50
//   bytes, each zone four (x, y, z, 1) corners and a 16-byte tail (stage
//   corpus reverse notes, Ada-Astra pass); the rest is found by shape: areas
//   laid out as EVE's, paths of points, runs of one repeated coefficient.
// - ITM: 16-byte header, `count` 20-byte records (item id, position, rotation
//   about Y), zero-padded to 16 bytes. Static item placements.
// - STE: 16-byte header, `count` 40-byte records: two u16, position, rotation
//   in degrees and scale, three floats each.
// - EST: 16-byte header (EST\0, u16 version, u16 count, u32 table offset); at
//   the table `count` rows of five u32 program offsets, one per difficulty
//   mode. A program is commands (low byte the code, next byte how many i32
//   arguments follow) ended by a zero word.
// - SEF: header (SEF\0, u16 section count, u16) and from +0x08 a table of
//   (u32 value, u32 offset) pairs up to the first section.
namespace dmcresource::stage_config {

enum class Kind : std::uint8_t { Pos, Eve, Cam, Itm, Ste, Est, Sef };

[[nodiscard]] const char* kind_name(Kind kind) noexcept;
// Which member the bytes are, by their tag; false when none.
[[nodiscard]] bool identify(std::span<const std::uint8_t> bytes, Kind* kind) noexcept;

struct Vec3 final {
    float x{}, y{}, z{};
};

struct Header final {
    std::uint16_t version{};
    std::uint16_t count{};
};

enum class Severity : std::uint8_t { Info, Warning, Error };

struct Note final {
    Severity severity{Severity::Info};
    std::string code;
    std::string message;
    std::uint64_t offset{};
};

struct Quad final {
    std::array<Vec3, 4> corners{};
    bool homogeneous{true};  // every corner's fourth component is 1
    bool level{true};        // the four corners share one height
};

struct PosRecord final {
    std::uint64_t offset{};
    std::uint32_t tag{};
    Vec3 position;
    float heading_degrees{};
    bool tail_zero{true};
};
struct PosDocument final {
    Header header;
    std::uint32_t capacity{};  // (size - 16) / 48
    std::vector<PosRecord> records;
};

struct EveRecord final {
    std::uint64_t offset{};
    std::uint32_t field0{};
    std::uint32_t kind{};  // groups the records
    std::uint32_t argument{};
    Quad area;
    float extent{};
    bool rest_zero{true};
};
struct EveDocument final {
    Header header;
    std::vector<EveRecord> records;
};

struct CamArea final {
    std::uint64_t offset{};
    Quad quad;
    float extent{};
    // Index into CamDocument::records of the 0x400 record holding it; -1 when
    // it was found by shape.
    int record{-1};
};
struct CamRecord final {
    std::uint64_t offset{};
    std::uint32_t flags{};
    std::uint32_t zone_count{};
    std::uint64_t size{};
};
struct CamPath final {
    std::uint64_t offset{};
    std::vector<Vec3> points;
};
struct CamRun final {
    std::uint64_t offset{};
    std::uint32_t count{};
    float value{};
};
struct CamDocument final {
    Header header;
    std::vector<CamRecord> records;  // the 0x400 family, in file order
    std::vector<CamArea> areas;
    std::vector<CamPath> paths;
    std::vector<CamRun> coefficient_runs;
    std::uint64_t unexplained_bytes{};
};

struct ItmRecord final {
    std::uint64_t offset{};
    std::uint32_t item_id{};
    Vec3 position;
    float rotation_y{};
};
struct ItmDocument final {
    Header header;
    std::vector<ItmRecord> records;
};

struct SteRecord final {
    std::uint64_t offset{};
    std::uint16_t kind{};
    std::uint16_t number{};
    Vec3 position;
    Vec3 rotation_degrees;
    Vec3 scale;
};
struct SteDocument final {
    Header header;
    std::vector<SteRecord> records;
};

inline constexpr std::size_t kEstModes = 5U;
inline constexpr std::array<const char*, kEstModes> kEstModeNames{
    "Easy", "Normal", "Hard", "Very Hard", "Dante Must Die"};
struct EstCommand final {
    std::uint64_t offset{};
    std::uint8_t code{};
    std::vector<std::int32_t> arguments;
};
struct EstProgram final {
    std::uint64_t offset{};
    std::vector<EstCommand> commands;
    bool terminated{};
};
struct EstDocument final {
    Header header;
    std::uint32_t table_offset{};
    std::vector<std::array<std::int32_t, kEstModes>> rows;  // indices into programs; -1 none
    std::vector<EstProgram> programs;
    std::uint64_t unexplained_bytes{};
};

struct SefSection final {
    std::uint32_t value{};
    std::uint64_t offset{};
    std::uint64_t size{};
};
struct SefDocument final {
    std::uint16_t sections_declared{};
    std::uint16_t field6{};
    std::vector<SefSection> sections;
};

template <typename Document>
struct Result final {
    bool recognized{};
    Document document;
    std::vector<Note> notes;

    [[nodiscard]] bool ok() const noexcept {
        if (!recognized) return false;
        for (const auto& note : notes) {
            if (note.severity == Severity::Error) return false;
        }
        return true;
    }
};

using Bytes = std::span<const std::uint8_t>;
[[nodiscard]] Result<PosDocument> read_pos(Bytes bytes);
[[nodiscard]] Result<EveDocument> read_eve(Bytes bytes);
[[nodiscard]] Result<CamDocument> read_cam(Bytes bytes);
[[nodiscard]] Result<ItmDocument> read_itm(Bytes bytes);
[[nodiscard]] Result<SteDocument> read_ste(Bytes bytes);
[[nodiscard]] Result<EstDocument> read_est(Bytes bytes);
[[nodiscard]] Result<SefDocument> read_sef(Bytes bytes);

}  // namespace dmcresource::stage_config
