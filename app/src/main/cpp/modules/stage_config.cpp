#include "dmcresource/stage_config.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <string>
#include <string_view>
#include <utility>

namespace dmcresource::stage_config {
namespace {

constexpr std::size_t kPosHeader = 0x10U;
constexpr std::size_t kPosRecord = 0x30U;
constexpr std::size_t kEveHeader = 0x10U;
constexpr std::size_t kEveRecord = 0x80U;
constexpr std::size_t kCamHeader = 0x10U;
constexpr std::uint32_t kCamZoneFlags = 0x400U;
constexpr std::size_t kCamRecordHead = 0x40U;
constexpr std::size_t kCamZone = 0x50U;
constexpr std::size_t kItmHeader = 0x10U;
constexpr std::size_t kItmRecord = 0x14U;
constexpr std::size_t kSteHeader = 0x10U;
constexpr std::size_t kSteRecord = 0x28U;
constexpr std::size_t kEstHeader = 0x10U;
// More arguments than this is not a command but bytes misread as one.
constexpr std::uint32_t kEstMaxArguments = 8U;

[[nodiscard]] std::uint32_t u32(Bytes bytes, std::size_t at) noexcept {
    std::uint32_t value = 0U;
    std::memcpy(&value, bytes.data() + at, sizeof value);
    return value;
}

[[nodiscard]] std::uint16_t u16(Bytes bytes, std::size_t at) noexcept {
    std::uint16_t value = 0U;
    std::memcpy(&value, bytes.data() + at, sizeof value);
    return value;
}

[[nodiscard]] float f32(Bytes bytes, std::size_t at) noexcept {
    float value = 0.0F;
    std::memcpy(&value, bytes.data() + at, sizeof value);
    return value;
}

[[nodiscard]] Vec3 vec3(Bytes bytes, std::size_t at) noexcept {
    return {f32(bytes, at), f32(bytes, at + 4U), f32(bytes, at + 8U)};
}

[[nodiscard]] bool zero(Bytes bytes, std::size_t from, std::size_t to) noexcept {
    return std::all_of(bytes.begin() + static_cast<std::ptrdiff_t>(from),
                       bytes.begin() + static_cast<std::ptrdiff_t>(to),
                       [](std::uint8_t value) { return value == 0U; });
}

template <typename Document>
void diagnose(Result<Document>& result, Severity severity, std::string code, std::string message,
              std::uint64_t offset) {
    result.notes.push_back({.severity = severity, .code = std::move(code), .message = std::move(message),
                                  .offset = offset});
}

/// The 16-byte header all three share: tag, u16 version, u16 count, then eight zero bytes.
template <typename Document>
[[nodiscard]] bool header(Bytes bytes, std::string_view tag, Result<Document>& result) {
    if (bytes.size() < 0x10U || std::memcmp(bytes.data(), tag.data(), 4U) != 0) {
        diagnose(result, Severity::Error, "stage-cfg.tag",
                 "The bytes do not open with the " + std::string{tag.substr(0, 3)} + " tag and a 16-byte header.",
                 0U);
        return false;
    }
    result.recognized = true;
    result.document.header = {.version = u16(bytes, 4U), .count = u16(bytes, 6U)};
    if (!zero(bytes, 8U, 16U)) {
        diagnose(result, Severity::Warning, "stage-cfg.header-tail",
                 "Header bytes 8..15 are not zero, as they are in every file seen.", 8U);
    }
    return true;
}

/// Four (x, y, z, w) corners at `at`.
[[nodiscard]] Quad quad(Bytes bytes, std::size_t at) noexcept {
    Quad result;
    for (std::size_t corner = 0U; corner < 4U; ++corner) {
        const auto base = at + corner * 16U;
        result.corners[corner] = vec3(bytes, base);
        result.homogeneous = result.homogeneous && f32(bytes, base + 12U) == 1.0F;
    }
    const float height = result.corners[0].y;
    for (const auto& corner : result.corners) {
        result.level = result.level && std::fabs(corner.y - height) <= 0.01F + std::fabs(height) * 1e-5F;
    }
    return result;
}

[[nodiscard]] bool finite_point(const Vec3& point) noexcept {
    return std::isfinite(point.x) && std::isfinite(point.y) && std::isfinite(point.z);
}

/// A point of a path: finite, every coordinate a real number and none zero,
/// within the range a stage spans. Bit patterns of small integers, which
/// read as denormals, are excluded by the lower bound.
[[nodiscard]] bool path_point(const Vec3& point) noexcept {
    const auto coordinate = [](float value) {
        const float magnitude = std::fabs(value);
        return std::isfinite(value) && magnitude >= 1e-3F && magnitude < 1e6F;
    };
    return coordinate(point.x) && coordinate(point.y) && coordinate(point.z);
}

} // namespace

const char* kind_name(Kind kind) noexcept {
    switch (kind) {
    case Kind::Pos: return "POS";
    case Kind::Eve: return "EVE";
    case Kind::Cam: return "CAM";
    case Kind::Itm: return "ITM";
    case Kind::Ste: return "STE";
    case Kind::Est: return "EST";
    case Kind::Sef: return "SEF";
    }
    return "?";
}

bool identify(Bytes bytes, Kind* kind) noexcept {
    if (bytes.size() < 0x10U || bytes[3] != 0U) return false;
    static constexpr std::array<std::pair<const char*, Kind>, 7> kTags{{
        {"POS", Kind::Pos}, {"EVE", Kind::Eve}, {"CAM", Kind::Cam}, {"ITM", Kind::Itm},
        {"STE", Kind::Ste}, {"EST", Kind::Est}, {"SEF", Kind::Sef},
    }};
    for (const auto& [tag, value] : kTags) {
        if (std::memcmp(bytes.data(), tag, 3U) == 0) {
            if (kind != nullptr) *kind = value;
            return true;
        }
    }
    return false;
}

Result<PosDocument> read_pos(Bytes bytes) {
    Result<PosDocument> result;
    if (!header(bytes, std::string_view{"POS\0", 4U}, result)) return result;
    auto& document = result.document;
    if ((bytes.size() - kPosHeader) % kPosRecord != 0U) {
        diagnose(result, Severity::Error, "pos.size",
                 "The file is not a 16-byte header and whole 48-byte records.", bytes.size());
        return result;
    }
    document.capacity = static_cast<std::uint32_t>((bytes.size() - kPosHeader) / kPosRecord);
    if (document.header.count > document.capacity) {
        diagnose(result, Severity::Error, "pos.count",
                 "The header counts " + std::to_string(document.header.count) + " records; the file holds " +
                     std::to_string(document.capacity) + ".",
                 6U);
        return result;
    }
    for (std::uint32_t index = 0U; index < document.capacity; ++index) {
        const auto at = kPosHeader + index * kPosRecord;
        if (index >= document.header.count) {
            if (!zero(bytes, at, at + kPosRecord)) {
                diagnose(result, Severity::Warning, "pos.spare-record",
                         "A record past the count is not zero.", at);
            }
            continue;
        }
        PosRecord record{
            .offset = at,
            .tag = u32(bytes, at),
            .position = vec3(bytes, at + 4U),
            .heading_degrees = f32(bytes, at + 16U),
            .tail_zero = zero(bytes, at + 20U, at + kPosRecord),
        };
        if (!finite_point(record.position) || !std::isfinite(record.heading_degrees)) {
            diagnose(result, Severity::Error, "pos.not-finite", "A position or heading is not a number.", at);
        }
        if (!record.tail_zero) {
            diagnose(result, Severity::Info, "pos.tail",
                     "Bytes after the heading are not zero; they are kept and not read.", at + 20U);
        }
        document.records.push_back(record);
    }
    return result;
}

Result<EveDocument> read_eve(Bytes bytes) {
    Result<EveDocument> result;
    if (!header(bytes, std::string_view{"EVE\0", 4U}, result)) return result;
    auto& document = result.document;
    const auto expected = kEveHeader + std::size_t{document.header.count} * kEveRecord;
    if (bytes.size() != expected) {
        diagnose(result, Severity::Error, "eve.size",
                 "The header counts " + std::to_string(document.header.count) + " records, which take " +
                     std::to_string(expected) + " bytes; the file has " + std::to_string(bytes.size()) + ".",
                 6U);
        return result;
    }
    for (std::uint32_t index = 0U; index < document.header.count; ++index) {
        const auto at = kEveHeader + index * kEveRecord;
        EveRecord record{
            .offset = at,
            .field0 = u32(bytes, at),
            .kind = u32(bytes, at + 4U),
            .argument = u32(bytes, at + 8U),
            .area = quad(bytes, at + 0x20U),
            .extent = f32(bytes, at + 0x60U),
            .rest_zero = zero(bytes, at + 0x0CU, at + 0x20U) && zero(bytes, at + 0x64U, at + kEveRecord),
        };
        if (!record.area.homogeneous) {
            diagnose(result, Severity::Warning, "eve.corner-w",
                     "A corner's fourth component is not 1.", at + 0x20U);
        }
        for (const auto& corner : record.area.corners) {
            if (!finite_point(corner)) {
                diagnose(result, Severity::Error, "eve.not-finite", "A corner is not a number.", at + 0x20U);
                break;
            }
        }
        if (!record.rest_zero) {
            diagnose(result, Severity::Info, "eve.unread-bytes",
                     "Bytes this reader does not name are not zero; they are kept.", at);
        }
        document.records.push_back(record);
    }
    return result;
}

Result<CamDocument> read_cam(Bytes bytes) {
    Result<CamDocument> result;
    if (!header(bytes, std::string_view{"CAM\0", 4U}, result)) return result;
    auto& document = result.document;
    if (bytes.size() % 4U != 0U) {
        diagnose(result, Severity::Error, "cam.size", "The file is not made of whole 32-bit words.",
                 bytes.size());
        return result;
    }
    std::size_t at = kCamHeader;
    // The 0x400 family from the first record on: flags, a zone count at
    // +0x14, 0x40 + count * 0x50 bytes, each zone four (x, y, z, 1) corners
    // and a 16-byte tail whose first float is the height. A record is taken
    // only when every zone it claims has that shape; the walk stops at the
    // first record that does not, and shape scanning reads the rest.
    while (at + kCamRecordHead <= bytes.size() && u32(bytes, at) == kCamZoneFlags) {
        const std::uint32_t zones = bytes[at + 0x14U];  // a byte (read_cam_shots)
        const auto size = kCamRecordHead + std::size_t{zones} * kCamZone;
        if (zones == 0U || at + size > bytes.size()) break;
        std::vector<CamArea> found;
        for (std::uint32_t zone = 0U; zone < zones; ++zone) {
            const auto base = at + kCamRecordHead + zone * kCamZone;
            const auto area = quad(bytes, base);
            bool finite = area.homogeneous;
            for (const auto& corner : area.corners) finite = finite && finite_point(corner);
            if (!finite) break;
            found.push_back({.offset = base, .quad = area, .extent = f32(bytes, base + 64U),
                             .record = static_cast<int>(document.records.size())});
        }
        if (found.size() != zones) break;
        document.records.push_back({.offset = at, .flags = kCamZoneFlags, .zone_count = zones, .size = size});
        document.areas.insert(document.areas.end(), found.begin(), found.end());
        at += size;
    }
    if (!document.records.empty() && document.records.size() < document.header.count) {
        diagnose(result, Severity::Info, "cam.records",
                 std::to_string(document.records.size()) + " of " + std::to_string(document.header.count) +
                     " records are zone records (0x400); the rest are read by shape.",
                 at);
    }
    while (at + 4U <= bytes.size()) {
        if (u32(bytes, at) == 0U) {
            at += 4U;
            continue;
        }
        // An area, as EVE lays one out: four (x, y, z, 1) corners on one
        // level, then a positive extent.
        if (at % 16U == 0U && at + 68U <= bytes.size()) {
            const auto area = quad(bytes, at);
            const float extent = f32(bytes, at + 64U);
            bool finite = true;
            for (const auto& corner : area.corners) finite = finite && path_point(corner);
            if (area.homogeneous && area.level && finite && std::isfinite(extent) && extent > 0.0F) {
                document.areas.push_back({.offset = at, .quad = area, .extent = extent});
                at += 68U;
                continue;
            }
        }
        // A run of one coefficient repeated at least three times, in (0, 16).
        {
            const float value = f32(bytes, at);
            std::size_t count = 0U;
            while (at + (count + 1U) * 4U <= bytes.size() && u32(bytes, at + count * 4U) == u32(bytes, at)) ++count;
            if (count >= 3U && std::isfinite(value) && value > 0.0F && value < 16.0F) {
                document.coefficient_runs.push_back(
                    {.offset = at, .count = static_cast<std::uint32_t>(count), .value = value});
                at += count * 4U;
                continue;
            }
        }
        // A path: two or more points in a row.
        {
            CamPath path{.offset = at, .points = {}};
            std::size_t cursor = at;
            while (cursor + 12U <= bytes.size()) {
                const auto point = vec3(bytes, cursor);
                // A homogeneous 1.0 after three coordinates belongs to an
                // area-shaped record, not to a path of points.
                if (!path_point(point)) break;
                path.points.push_back(point);
                cursor += 12U;
            }
            if (path.points.size() >= 2U) {
                document.paths.push_back(std::move(path));
                at = cursor;
                continue;
            }
        }
        document.unexplained_bytes += 4U;
        at += 4U;
    }
    if (document.unexplained_bytes > 0U) {
        diagnose(result, Severity::Info, "cam.unexplained",
                 std::to_string(document.unexplained_bytes) +
                     " non-zero bytes fit no shape this reader knows; the record layout is open.",
                 kCamHeader);
    }
    return result;
}

Result<ItmDocument> read_itm(Bytes bytes) {
    Result<ItmDocument> result;
    if (!header(bytes, std::string_view{"ITM\0", 4U}, result)) return result;
    auto& document = result.document;
    const auto used = kItmHeader + std::size_t{document.header.count} * kItmRecord;
    const auto padded = (used + 15U) & ~std::size_t{15U};
    if (bytes.size() < used) {
        diagnose(result, Severity::Error, "itm.size",
                 "The header counts " + std::to_string(document.header.count) + " records, which take " +
                     std::to_string(used) + " bytes; the file has " + std::to_string(bytes.size()) + ".",
                 6U);
        return result;
    }
    if (bytes.size() != padded) {
        diagnose(result, Severity::Warning, "itm.padding",
                 "Records are padded to a multiple of 16 bytes in every file seen; this one is " +
                     std::to_string(bytes.size()) + " bytes, not " + std::to_string(padded) + ".",
                 used);
    }
    if (!zero(bytes, used, bytes.size())) {
        diagnose(result, Severity::Info, "itm.tail", "Bytes after the last record are not zero; they are kept.",
                 used);
    }
    for (std::uint32_t index = 0U; index < document.header.count; ++index) {
        const auto at = kItmHeader + index * kItmRecord;
        ItmRecord record{.offset = at, .item_id = u32(bytes, at), .position = vec3(bytes, at + 4U),
                         .rotation_y = f32(bytes, at + 16U)};
        if (!finite_point(record.position) || !std::isfinite(record.rotation_y)) {
            diagnose(result, Severity::Error, "itm.not-finite", "A position or rotation is not a number.", at);
        }
        document.records.push_back(record);
    }
    return result;
}

Result<SteDocument> read_ste(Bytes bytes) {
    Result<SteDocument> result;
    if (!header(bytes, std::string_view{"STE\0", 4U}, result)) return result;
    auto& document = result.document;
    const auto used = kSteHeader + std::size_t{document.header.count} * kSteRecord;
    if (bytes.size() < used) {
        diagnose(result, Severity::Error, "ste.size",
                 "The header counts " + std::to_string(document.header.count) + " records, which take " +
                     std::to_string(used) + " bytes; the file has " + std::to_string(bytes.size()) + ".",
                 6U);
        return result;
    }
    if (!zero(bytes, used, bytes.size())) {
        diagnose(result, Severity::Info, "ste.tail", "Bytes after the last record are not zero; they are kept.",
                 used);
    }
    for (std::uint32_t index = 0U; index < document.header.count; ++index) {
        const auto at = kSteHeader + index * kSteRecord;
        SteRecord record{
            .offset = at,
            .kind = u16(bytes, at),
            .number = u16(bytes, at + 2U),
            .position = vec3(bytes, at + 4U),
            .rotation_degrees = vec3(bytes, at + 16U),
            .scale = vec3(bytes, at + 28U),
        };
        if (!finite_point(record.position) || !finite_point(record.rotation_degrees) || !finite_point(record.scale)) {
            diagnose(result, Severity::Error, "ste.not-finite", "A transform value is not a number.", at);
        }
        document.records.push_back(record);
    }
    return result;
}

Result<EstDocument> read_est(Bytes bytes) {
    Result<EstDocument> result;
    if (bytes.size() < kEstHeader || std::memcmp(bytes.data(), "EST\0", 4U) != 0) {
        diagnose(result, Severity::Error, "stage-cfg.tag",
                 "The bytes do not open with the EST tag and a 16-byte header.", 0U);
        return result;
    }
    result.recognized = true;
    auto& document = result.document;
    document.header = {.version = u16(bytes, 4U), .count = u16(bytes, 6U)};
    document.table_offset = u32(bytes, 8U);
    if (!zero(bytes, 12U, 16U)) {
        diagnose(result, Severity::Warning, "stage-cfg.header-tail",
                 "Header bytes 12..15 are not zero, as they are in every file seen.", 12U);
    }
    const auto table = std::size_t{document.table_offset};
    const auto table_end = table + std::size_t{document.header.count} * kEstModes * 4U;
    if (table < kEstHeader || table % 4U != 0U || table_end > bytes.size()) {
        diagnose(result, Severity::Error, "est.table",
                 "The table of " + std::to_string(document.header.count) + " rows of five offsets at " +
                     std::to_string(table) + " does not fit the file.",
                 8U);
        return result;
    }
    if (!zero(bytes, table_end, bytes.size())) {
        diagnose(result, Severity::Info, "est.tail", "Bytes after the table are not zero; they are kept.",
                 table_end);
    }

    // Each distinct offset is one program; rows index into them.
    std::vector<std::uint32_t> starts;
    for (std::size_t at = table; at < table_end; at += 4U) {
        const auto start = u32(bytes, at);
        if (start != 0U && std::find(starts.begin(), starts.end(), start) == starts.end()) starts.push_back(start);
    }
    std::sort(starts.begin(), starts.end());
    std::vector<bool> read(table, false);
    for (const auto start : starts) {
        EstProgram program{.offset = start, .commands = {}, .terminated = false};
        if (start < kEstHeader || start % 4U != 0U || start >= table) {
            diagnose(result, Severity::Error, "est.offset",
                     "A program offset " + std::to_string(start) + " lies outside the bytes between header and table.",
                     table);
            document.programs.push_back(std::move(program));
            continue;
        }
        std::size_t at = start;
        while (at + 4U <= table) {
            const auto word = u32(bytes, at);
            std::fill(read.begin() + static_cast<std::ptrdiff_t>(at), read.begin() + static_cast<std::ptrdiff_t>(at + 4U),
                      true);
            if (word == 0U) {
                program.terminated = true;
                break;
            }
            const auto arguments = (word >> 8U) & 0xFFU;
            if ((word >> 16U) != 0U || arguments > kEstMaxArguments || at + 4U + arguments * 4U > table) {
                diagnose(result, Severity::Error, "est.command",
                         "A word that is not a command: " + std::to_string(word) + ".", at);
                break;
            }
            EstCommand command{.offset = at, .code = static_cast<std::uint8_t>(word & 0xFFU), .arguments = {}};
            for (std::uint32_t argument = 0U; argument < arguments; ++argument) {
                const auto where = at + 4U + argument * 4U;
                command.arguments.push_back(static_cast<std::int32_t>(u32(bytes, where)));
                std::fill(read.begin() + static_cast<std::ptrdiff_t>(where),
                          read.begin() + static_cast<std::ptrdiff_t>(where + 4U), true);
            }
            at += 4U + arguments * 4U;
            program.commands.push_back(std::move(command));
        }
        if (!program.terminated) {
            diagnose(result, Severity::Warning, "est.unterminated",
                     "A program runs to the table without a zero word to end it.", start);
        }
        document.programs.push_back(std::move(program));
    }
    for (std::uint32_t row = 0U; row < document.header.count; ++row) {
        std::array<std::int32_t, kEstModes> indices{};
        for (std::size_t mode = 0U; mode < kEstModes; ++mode) {
            const auto start = u32(bytes, table + (row * kEstModes + mode) * 4U);
            const auto found = std::find(starts.begin(), starts.end(), start);
            indices[mode] = start == 0U ? -1 : static_cast<std::int32_t>(found - starts.begin());
        }
        document.rows.push_back(indices);
    }
    for (std::size_t at = kEstHeader; at < table; ++at) {
        if (!read[at] && bytes[at] != 0U) ++document.unexplained_bytes;
    }
    if (document.unexplained_bytes > 0U) {
        diagnose(result, Severity::Info, "est.unexplained",
                 std::to_string(document.unexplained_bytes) + " non-zero bytes lie in no program the table names.",
                 kEstHeader);
    }
    return result;
}

Result<SefDocument> read_sef(Bytes bytes) {
    Result<SefDocument> result;
    if (bytes.size() < 0x10U || std::memcmp(bytes.data(), "SEF\0", 4U) != 0) {
        diagnose(result, Severity::Error, "stage-cfg.tag", "The bytes do not open with the SEF tag.", 0U);
        return result;
    }
    result.recognized = true;
    auto& document = result.document;
    document.sections_declared = u16(bytes, 4U);
    document.field6 = u16(bytes, 6U);
    // The table runs from +0x08 up to the first section it names.
    const auto first = std::size_t{u32(bytes, 12U)};
    if (first < 0x10U || first > bytes.size() || (first - 8U) % 8U != 0U) {
        diagnose(result, Severity::Error, "sef.table",
                 "The first section offset " + std::to_string(first) + " does not end a table of (value, offset) pairs.",
                 12U);
        return result;
    }
    const auto entries = (first - 8U) / 8U;
    if (entries != document.sections_declared) {
        diagnose(result, Severity::Warning, "sef.count",
                 "The header names " + std::to_string(document.sections_declared) + " sections; the table holds " +
                     std::to_string(entries) + ".",
                 4U);
    }
    std::uint64_t previous = first;
    for (std::size_t index = 0U; index < entries; ++index) {
        const auto at = 8U + index * 8U;
        SefSection section{.value = u32(bytes, at), .offset = u32(bytes, at + 4U), .size = 0U};
        if (section.offset < previous || section.offset > bytes.size()) {
            diagnose(result, Severity::Error, "sef.offset",
                     "Section " + std::to_string(index) + " starts at " + std::to_string(section.offset) +
                         ", before the one ahead of it or past the end.",
                     at + 4U);
            return result;
        }
        previous = section.offset;
        document.sections.push_back(section);
    }
    for (std::size_t index = 0U; index < document.sections.size(); ++index) {
        const auto end = index + 1U < document.sections.size() ? document.sections[index + 1U].offset : bytes.size();
        document.sections[index].size = end - document.sections[index].offset;
    }
    return result;
}

namespace {

constexpr std::uint32_t kCamFixed = 0x000U;
constexpr std::uint32_t kCamRail = 0x100U;
constexpr std::size_t kCamFixedHead = 0xB0U;
constexpr std::size_t kCamRailHead = 0x490U;
constexpr std::size_t kCamRailPoints = 32U;
constexpr std::uint32_t kCamFollowsPlayer = 0x200U;

[[nodiscard]] float area_of(const Quad& quad) noexcept {
    float sum = 0.0F;
    for (std::size_t i = 0U; i < 4U; ++i) {
        const auto& a = quad.corners[i];
        const auto& b = quad.corners[(i + 1U) % 4U];
        sum += a.x * b.z - b.x * a.z;
    }
    return std::fabs(sum) * 0.5F;
}

// The nearest place on a polyline (horizontally) to `point`: the segment and
// how far along it.
[[nodiscard]] std::pair<std::size_t, float> nearest_on(const std::vector<Vec3>& rail, const Vec3& point) noexcept {
    std::pair<std::size_t, float> best{0U, 0.0F};
    float best_distance = std::numeric_limits<float>::max();
    for (std::size_t i = 0U; i + 1U < rail.size(); ++i) {
        const float dx = rail[i + 1U].x - rail[i].x, dz = rail[i + 1U].z - rail[i].z;
        const float length = dx * dx + dz * dz;
        float t = length > 0.0F ? ((point.x - rail[i].x) * dx + (point.z - rail[i].z) * dz) / length : 0.0F;
        t = std::clamp(t, 0.0F, 1.0F);
        const float px = rail[i].x + dx * t - point.x, pz = rail[i].z + dz * t - point.z;
        const float distance = px * px + pz * pz;
        if (distance < best_distance) {
            best_distance = distance;
            best = {i, t};
        }
    }
    return best;
}

[[nodiscard]] Vec3 lerp(const Vec3& a, const Vec3& b, float t) noexcept {
    return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t};
}

}  // namespace

std::vector<CamShot> read_cam_shots(Bytes bytes) {
    std::vector<CamShot> shots;
    if (bytes.size() < kCamHeader || std::memcmp(bytes.data(), "CAM\0", 4U) != 0) return {};
    const std::size_t count = u16(bytes, 6U);
    std::size_t at = kCamHeader;
    for (std::size_t record = 0U; record < count; ++record) {
        if (at + kCamRecordHead > bytes.size()) return {};
        CamShot shot{.offset = at, .type = u32(bytes, at), .flags = u32(bytes, at + 0x40U)};
        std::size_t head = 0U;
        if (shot.type == kCamFixed) {
            head = kCamFixedHead;
        } else if (shot.type == kCamRail) {
            head = kCamRailHead;
        } else if (shot.type == kCamZoneFlags) {
            head = kCamRecordHead;
        } else {
            return {};
        }
        // The zone count is the byte at +0x14 (st002 keeps 01 78 in the next
        // two: `02 01 78`).
        const std::size_t zones = bytes[at + 0x14U];
        if (zones == 0U || at + head + zones * kCamZone > bytes.size()) return {};
        if (shot.type == kCamFixed) {
            shot.eye = {vec3(bytes, at + 0x90U)};
            shot.target = {vec3(bytes, at + 0x50U)};
            shot.fov = {f32(bytes, at + 0xA0U)};
            shot.follows_player = (shot.flags & kCamFollowsPlayer) != 0U;
        } else if (shot.type == kCamRail) {
            const std::size_t points = u32(bytes, at + 0x480U);
            if (points == 0U || points > kCamRailPoints) return {};
            for (std::size_t k = 0U; k < points; ++k) {
                shot.eye.push_back(vec3(bytes, at + 0x80U + k * 12U));
                shot.target.push_back(vec3(bytes, at + 0x200U + k * 12U));
                shot.fov.push_back(f32(bytes, at + 0x380U + k * 4U));
            }
        }
        for (const auto& p : shot.eye) {
            if (!finite_point(p)) return {};
        }
        for (const auto& p : shot.target) {
            if (!finite_point(p)) return {};
        }
        for (std::size_t zone = 0U; zone < zones; ++zone) {
            const auto base = at + head + zone * kCamZone;
            const auto area = quad(bytes, base);
            if (!area.homogeneous) return {};
            shot.zones.push_back({.offset = base, .quad = area, .extent = f32(bytes, base + 64U),
                                  .record = static_cast<int>(record)});
        }
        shots.push_back(std::move(shot));
        at += head + zones * kCamZone;
    }
    // Nothing but zero padding after the last record.
    if (!zero(bytes, at, bytes.size())) return {};
    return shots;
}

bool zone_holds(const CamArea& zone, const Vec3& point) noexcept {
    const auto& c = zone.quad.corners;
    if (point.y < c[0].y - 1.0F || point.y > c[0].y + zone.extent) return false;
    // Inside a convex quadrilateral either way round: every edge on one side.
    int sign = 0;
    for (std::size_t i = 0U; i < 4U; ++i) {
        const auto& a = c[i];
        const auto& b = c[(i + 1U) % 4U];
        const float cross = (b.x - a.x) * (point.z - a.z) - (b.z - a.z) * (point.x - a.x);
        const int side = cross > 0.0F ? 1 : (cross < 0.0F ? -1 : 0);
        if (side == 0) continue;
        if (sign == 0) sign = side;
        if (side != sign) return false;
    }
    return true;
}

std::optional<CamView> cam_view_at(const std::vector<CamShot>& shots, const Vec3& player) {
    int best = -1;
    float best_area = std::numeric_limits<float>::max();
    for (std::size_t i = 0U; i < shots.size(); ++i) {
        if (shots[i].eye.empty()) continue;
        for (const auto& zone : shots[i].zones) {
            const float area = area_of(zone.quad);
            if (zone_holds(zone, player) && area < best_area) {
                best = static_cast<int>(i);
                best_area = area;
            }
        }
    }
    if (best < 0) return std::nullopt;
    const auto& shot = shots[static_cast<std::size_t>(best)];
    CamView view{.shot = best};
    if (shot.type == kCamFixed) {
        view.eye = shot.eye.front();
        const auto& t = shot.target.front();
        view.target = shot.follows_player ? Vec3{player.x + t.x, player.y + t.y, player.z + t.z} : t;
        view.fov_radians = shot.fov.front();
    } else if (shot.eye.size() == 1U) {
        view.eye = shot.eye.front();
        view.target = shot.target.front();
        view.fov_radians = shot.fov.front();
    } else {
        const auto [segment, t] = nearest_on(shot.target, player);
        view.eye = lerp(shot.eye[segment], shot.eye[segment + 1U], t);
        view.target = lerp(shot.target[segment], shot.target[segment + 1U], t);
        view.fov_radians = shot.fov[segment] + (shot.fov[segment + 1U] - shot.fov[segment]) * t;
    }
    if (!(view.fov_radians > 0.1F && view.fov_radians < 3.0F)) view.fov_radians = 1.0F;
    return view;
}

}  // namespace dmcresource::stage_config
