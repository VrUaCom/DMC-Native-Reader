#include "dmcresource/dmc_resource.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace dmcresource {
namespace {

struct ExtensionRule {
    const char* extension;
    Format decode_format;
    const char* family;
    const char* domain;
    const char* support;
    const char* evidence;
    const char* mime;
};

constexpr std::array<ExtensionRule, 72> kRules{{
    {"nbz", Format::Other, "NBZ", "container", "structural", "EXE_CONFIRMED", "application/vnd.dmc.nbz"},
    {"pac", Format::Other, "PAC", "container", "structural", "EXE_CONFIRMED", "application/vnd.dmc.pac"},
    {"pnst", Format::Other, "PNST", "container", "structural", "EXE_CONFIRMED", "application/vnd.dmc.pnst"},
    {"pack", Format::Other, "PACK", "container", "research-only", "DATA_CONFIRMED", "application/vnd.dmc.pack"},
    {"index", Format::Other, ".index", "metadata", "recognized", "DATA_CONFIRMED", "text/plain"},
    {"lst", Format::Other, ".lst", "metadata", "runtime-only", "EXE_CONFIRMED", "text/plain"},
    {"afs", Format::Other, "AFS namespace", "namespace", "recognized", "EXE_CONFIRMED", "application/octet-stream"},
    {"ukn", Format::Other, ".ukn", "metadata", "fallback-only", "DATA_CONFIRMED", "application/octet-stream"},
    {"bin", Format::Other, ".bin", "metadata", "fallback-only", "EXE_CONFIRMED", "application/octet-stream"},
    {"scm", Format::Scm, "SCM", "geometry", "mesh-preview", "EXE_CONFIRMED", "application/vnd.dmc.scm"},
    {"mod", Format::Mod, "MOD", "geometry", "mesh-preview", "EXE_CONFIRMED", "application/vnd.dmc.mod"},
    {"hits", Format::Other, "HITS", "collision", "structural", "DATA_CONFIRMED", "application/vnd.dmc.hits"},
    {"dca", Format::Other, "DCA", "camera", "structural", "HIGH_CONFIDENCE", "application/vnd.dmc.dca"},
    {"dds", Format::Other, "DDS", "texture", "recognized", "DATA_CONFIRMED", "image/vnd-ms.dds"},
    {"ptx", Format::Other, "PTX", "texture", "recognized", "EXE_CONFIRMED", "application/vnd.dmc.ptx"},
    {"tm2", Format::Other, "TIM2", "texture", "research-only", "EXE_CONFIRMED", "application/vnd.dmc.tm2"},
    {"ptz", Format::Other, "PTZ", "texture", "research-only", "HIGH_CONFIDENCE", "application/vnd.dmc.ptz"},
    {"efm", Format::Other, "EFM", "geometry", "runtime-only", "EXE_CONFIRMED", "application/vnd.dmc.efm"},
    {"shw", Format::Other, "SHW", "render", "runtime-only", "EXE_CONFIRMED", "application/vnd.dmc.shw"},
    {"mrp", Format::Other, "MRP", "render", "runtime-only", "HIGH_CONFIDENCE", "application/vnd.dmc.mrp"},
    {"sef", Format::Other, "SEF", "effect", "research-only", "HIGH_CONFIDENCE", "application/vnd.dmc.sef"},
    {"efe", Format::Other, "EFE", "effect", "research-only", "RESEARCH_REQUIRED", "application/vnd.dmc.efe"},
    {"efw", Format::Other, "EFW", "effect", "research-only", "RESEARCH_REQUIRED", "application/vnd.dmc.efw"},
    {"c1d", Format::Other, "C1D", "cloth-simulation", "runtime-only", "HIGH_CONFIDENCE", "application/vnd.dmc.c1d"},
    {"clt", Format::Other, "CLT", "cloth-simulation", "runtime-only", "HIGH_CONFIDENCE", "application/vnd.dmc.clt"},
    {"mot", Format::Other, "MOT", "animation", "research-only", "EXE_CONFIRMED", "application/vnd.dmc.mot"},
    {"mot2", Format::Other, "MOT2", "animation", "research-only", "EXE_CONFIRMED", "application/vnd.dmc.mot"},
    {"mot3", Format::Other, "MOT3", "animation", "research-only", "EXE_CONFIRMED", "application/vnd.dmc.mot"},
    {"mot4", Format::Other, "MOT4", "animation", "research-only", "EXE_CONFIRMED", "application/vnd.dmc.mot"},
    {"mot5", Format::Other, "MOT5", "animation", "research-only", "EXE_CONFIRMED", "application/vnd.dmc.mot"},
    {"mot6", Format::Other, "MOT6", "animation", "research-only", "EXE_CONFIRMED", "application/vnd.dmc.mot"},
    {"mcv", Format::Other, "MCV", "animation", "runtime-only", "HIGH_CONFIDENCE", "application/vnd.dmc.mcv"},
    {"cam", Format::Other, "CAM", "camera", "recognized", "EXE_CONFIRMED", "application/vnd.dmc.cam"},
    {"hid", Format::Other, "HID", "visibility-control", "research-only", "HIGH_CONFIDENCE", "application/vnd.dmc.hid"},
    {"hid2", Format::Other, "HID2", "visibility-control", "research-only", "HIGH_CONFIDENCE", "application/vnd.dmc.hid"},
    {"hid3", Format::Other, "HID3", "visibility-control", "research-only", "HIGH_CONFIDENCE", "application/vnd.dmc.hid"},
    {"tsc", Format::Other, "TSC", "animation-control", "runtime-only", "RESEARCH_REQUIRED", "application/vnd.dmc.tsc"},
    {"lig", Format::Other, "LIG", "lighting", "structural", "DATA_CONFIRMED", "application/vnd.dmc.lig"},
    {"lig2", Format::Other, "LIG2", "lighting", "structural", "HIGH_CONFIDENCE", "application/vnd.dmc.lig2"},
    {"eve", Format::Other, "EVE", "event-volume", "research-only", "DATA_CONFIRMED", "application/vnd.dmc.eve"},
    {"pos", Format::Other, "POS", "stage-placement", "research-only", "HIGH_CONFIDENCE", "application/vnd.dmc.pos"},
    {"itm", Format::Other, "ITM", "item-placement", "recognized", "EXE_CONFIRMED", "application/vnd.dmc.itm"},
    {"ste", Format::Other, "STE", "stage-transform", "research-only", "HIGH_CONFIDENCE", "application/vnd.dmc.ste"},
    {"est", Format::Other, "EST", "stage-dependency-control", "research-only", "CANDIDATE", "application/vnd.dmc.est"},
    {"txt", Format::Other, "TXT", "script-config", "structural", "EXE_CONFIRMED", "text/plain"},
    {"adx", Format::Other, "ADX", "audio", "runtime-only", "EXE_CONFIRMED", "audio/x-adx"},
    {"ogg", Format::Other, "OGG", "audio", "runtime-only", "EXE_CONFIRMED", "audio/ogg"},
    {"vag", Format::Other, "VAGp", "audio", "research-only", "HIGH_CONFIDENCE", "audio/x-vag"},
    {"phd", Format::Other, "PHD", "audio-bank", "research-only", "HIGH_CONFIDENCE", "application/vnd.dmc.phd"},
    {"tsb", Format::Other, "TSB", "audio-bank", "research-only", "HIGH_CONFIDENCE", "application/vnd.dmc.tsb"},
    {"bd", Format::Other, "BD", "audio-bank", "research-only", "HIGH_CONFIDENCE", "application/vnd.dmc.bd"},
    {"sfd", Format::Other, "SFD", "video", "runtime-only", "EXE_CONFIRMED", "application/vnd.dmc.sfd"},
    {"wmv", Format::Other, "WMV", "video", "runtime-only", "EXE_CONFIRMED", "video/x-ms-wmv"},
    {"pss", Format::Other, "PSS", "media-capability", "capability-only", "EXE_CONFIRMED", "application/octet-stream"},
    {"thp", Format::Other, "THP", "media-capability", "capability-only", "EXE_CONFIRMED", "application/octet-stream"},
    {"pam", Format::Other, "PAM", "media-capability", "capability-only", "EXE_CONFIRMED", "application/octet-stream"},
    {"xmv", Format::Other, "XMV", "media-capability", "capability-only", "EXE_CONFIRMED", "application/octet-stream"},
    {"pmf", Format::Other, "PMF", "media-capability", "capability-only", "EXE_CONFIRMED", "application/octet-stream"},
    {"avi", Format::Other, "AVI", "media-capability", "capability-only", "EXE_CONFIRMED", "video/x-msvideo"},
    {"mpg", Format::Other, "MPG", "media-capability", "capability-only", "EXE_CONFIRMED", "video/mpeg"},
    {"bik", Format::Other, "BIK", "media-capability", "capability-only", "EXE_CONFIRMED", "application/octet-stream"},
    {"mp4", Format::Other, "MP4", "media-capability", "capability-only", "EXE_CONFIRMED", "video/mp4"},
    {"sav", Format::Other, "SAV", "persistence", "structural", "DATA_CONFIRMED", "application/vnd.dmc.save"},
    {"fon", Format::Other, "FON", "font", "research-only", "HIGH_CONFIDENCE", "application/vnd.dmc.fon"},
    {"ico", Format::Other, "ICO", "legacy-save-ui", "research-only", "HIGH_CONFIDENCE", "image/x-icon"},
    {"sys", Format::Other, "icon.sys", "legacy-save-ui", "research-only", "HIGH_CONFIDENCE", "application/octet-stream"},
    {"ptx2", Format::Other, "PTX", "texture", "recognized", "EXE_CONFIRMED", "application/vnd.dmc.ptx"},
    {"mpeg", Format::Other, "MPG", "media-capability", "capability-only", "EXE_CONFIRMED", "video/mpeg"},
    {"vagp", Format::Other, "VAGp", "audio", "research-only", "HIGH_CONFIDENCE", "audio/x-vag"},
    {"eventtbl", Format::Other, "EventTbl", "mission-bytecode", "research-only", "HIGH_CONFIDENCE", "application/octet-stream"},
    {"options", Format::Other, "options.sav", "persistence", "research-only", "HIGH_CONFIDENCE", "application/vnd.dmc.save"},
    {"dmc3sav", Format::Other, "dmc3.sav", "persistence", "structural", "DATA_CONFIRMED", "application/vnd.dmc.save"},
}};

std::string lower_copy(std::string_view text) {
    std::string out(text);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return out;
}

std::string lower_extension(std::string_view filename) {
    const auto dot = filename.find_last_of('.');
    if (dot == std::string_view::npos || dot + 1 >= filename.size()) return {};
    return lower_copy(filename.substr(dot + 1));
}

std::string lower_basename(std::string_view filename) {
    const auto slash = filename.find_last_of("/\\");
    return lower_copy(slash == std::string_view::npos ? filename : filename.substr(slash + 1));
}

bool has_prefix(const std::uint8_t* p, std::size_t n, std::string_view signature) noexcept {
    if (p == nullptr || n < signature.size()) return false;
    for (std::size_t i = 0; i < signature.size(); ++i) {
        if (p[i] != static_cast<std::uint8_t>(signature[i])) return false;
    }
    return true;
}

bool has_magic4(const std::uint8_t* p, std::size_t n,
                char a, char b, char c, char d) noexcept {
    return p != nullptr && n >= 4 &&
           p[0] == static_cast<std::uint8_t>(a) &&
           p[1] == static_cast<std::uint8_t>(b) &&
           p[2] == static_cast<std::uint8_t>(c) &&
           p[3] == static_cast<std::uint8_t>(d);
}

std::uint32_t read_u32_le(const std::uint8_t* p) noexcept {
    return static_cast<std::uint32_t>(p[0]) |
           (static_cast<std::uint32_t>(p[1]) << 8u) |
           (static_cast<std::uint32_t>(p[2]) << 16u) |
           (static_cast<std::uint32_t>(p[3]) << 24u);
}

ProbeResult make_result(Format format, bool content_confirmed,
                        const char* family, const char* domain,
                        const char* support, const char* evidence,
                        const char* mime) noexcept {
    return {format, true, content_confirmed, family, domain, support, evidence, mime};
}

ProbeResult from_extension(std::string_view extension) noexcept {
    for (const auto& rule : kRules) {
        if (extension == rule.extension) {
            return make_result(rule.decode_format, false, rule.family, rule.domain,
                               rule.support, rule.evidence, rule.mime);
        }
    }
    return {};
}

std::string describe_slot_container(const std::uint8_t* bytes, std::size_t size,
                                    const char* family) {
    std::ostringstream out;
    if (bytes == nullptr || size < 8) {
        out << family << " truncated header";
        return out.str();
    }
    const std::uint32_t slots = read_u32_le(bytes + 4);
    if (slots > (size - 8u) / 4u) {
        out << family << " invalid offset table: declared_slots=" << slots;
        return out.str();
    }
    std::size_t populated = 0;
    std::size_t empty = 0;
    std::size_t aliases = 0;
    std::vector<std::uint32_t> seen;
    seen.reserve(slots);
    const std::size_t table_end = 8u + static_cast<std::size_t>(slots) * 4u;
    bool bounds_ok = true;
    for (std::uint32_t i = 0; i < slots; ++i) {
        const auto offset = read_u32_le(bytes + 8u + static_cast<std::size_t>(i) * 4u);
        if (offset == 0u) {
            ++empty;
            continue;
        }
        ++populated;
        if (offset < table_end || offset >= size) bounds_ok = false;
        if (std::find(seen.begin(), seen.end(), offset) != seen.end()) {
            ++aliases;
        } else {
            seen.push_back(offset);
        }
    }
    out << family << " relative-slot container"
        << " | slots=" << slots
        << " populated=" << populated
        << " empty=" << empty
        << " aliases=" << aliases
        << " bounds=" << (bounds_ok ? "OK" : "INVALID");
    return out.str();
}

}  // namespace

ProbeResult probe(std::string_view filename,
                  const std::uint8_t* bytes,
                  std::size_t size) noexcept {
    // Binary/content identities with stronger evidence take precedence over names.
    if (has_prefix(bytes, size, "MZ")) {
        return make_result(Format::Other, true, "PE", "executable", "structural",
                           "EXE_CONFIRMED", "application/vnd.microsoft.portable-executable");
    }
    if (has_magic4(bytes, size, 'P', 'A', 'C', '\0')) {
        return make_result(Format::Other, true, "PAC", "container", "structural",
                           "EXE_CONFIRMED", "application/vnd.dmc.pac");
    }
    if (has_magic4(bytes, size, 'P', 'N', 'S', 'T')) {
        return make_result(Format::Other, true, "PNST", "container", "structural",
                           "EXE_CONFIRMED", "application/vnd.dmc.pnst");
    }
    if (has_magic4(bytes, size, 'S', 'C', 'M', ' ')) {
        return make_result(Format::Scm, true, "SCM", "geometry", "mesh-preview",
                           "EXE_CONFIRMED", "application/vnd.dmc.scm");
    }
    if (has_magic4(bytes, size, 'M', 'O', 'D', ' ')) {
        return make_result(Format::Mod, true, "MOD", "geometry", "mesh-preview",
                           "EXE_CONFIRMED", "application/vnd.dmc.mod");
    }
    if (has_prefix(bytes, size, "EFM")) {
        return make_result(Format::Other, true, "EFM", "geometry", "runtime-only",
                           "EXE_CONFIRMED", "application/vnd.dmc.efm");
    }
    if (has_prefix(bytes, size, "MRP")) {
        return make_result(Format::Other, true, "MRP", "render", "runtime-only",
                           "HIGH_CONFIDENCE", "application/vnd.dmc.mrp");
    }
    if (has_prefix(bytes, size, "SHW")) {
        return make_result(Format::Other, true, "SHW", "render", "runtime-only",
                           "EXE_CONFIRMED", "application/vnd.dmc.shw");
    }
    if (has_prefix(bytes, size, "MCV ")) {
        return make_result(Format::Other, true, "MCV", "animation", "runtime-only",
                           "HIGH_CONFIDENCE", "application/vnd.dmc.mcv");
    }
    if (has_prefix(bytes, size, "EFE")) {
        return make_result(Format::Other, true, "EFE", "effect", "research-only",
                           "RESEARCH_REQUIRED", "application/vnd.dmc.efe");
    }
    if (has_prefix(bytes, size, "EFW")) {
        return make_result(Format::Other, true, "EFW", "effect", "research-only",
                           "RESEARCH_REQUIRED", "application/vnd.dmc.efw");
    }
    // HITS is a four-byte collision payload identity observed in data/parsers.
    // It is intentionally NOT described as an EXE registry tag.  HITS$ is rejected.
    if (has_prefix(bytes, size, "HITS")) {
        return make_result(Format::Other, true, "HITS", "collision", "structural",
                           "DATA_CONFIRMED", "application/vnd.dmc.hits");
    }
    if (has_magic4(bytes, size, 'D', 'C', 'A', '\0')) {
        return make_result(Format::Other, true, "DCA", "camera", "structural",
                           "HIGH_CONFIDENCE", "application/vnd.dmc.dca");
    }
    if (has_prefix(bytes, size, "DDS ")) {
        return make_result(Format::Other, true, "DDS", "texture", "recognized",
                           "DATA_CONFIRMED", "image/vnd-ms.dds");
    }
    if (has_prefix(bytes, size, "TIM2")) {
        return make_result(Format::Other, true, "TIM2", "texture", "research-only",
                           "EXE_CONFIRMED", "application/vnd.dmc.tm2");
    }
    if (has_prefix(bytes, size, "OggS")) {
        return make_result(Format::Other, true, "OGG", "audio", "runtime-only",
                           "EXE_CONFIRMED", "audio/ogg");
    }
    if (has_prefix(bytes, size, "SPUMAPDT")) {
        return make_result(Format::Other, true, "SPUMAPDT", "audio-bank", "runtime-only",
                           "EXE_CONFIRMED", "application/vnd.dmc.spumapdt");
    }
    if (size >= 12 && has_prefix(bytes, size, "RIFF") &&
        bytes[8] == 'A' && bytes[9] == 'V' && bytes[10] == 'I' && bytes[11] == ' ') {
        return make_result(Format::Other, true, "AVI", "media-capability", "capability-only",
                           "EXE_CONFIRMED", "video/x-msvideo");
    }
    if (size >= 8 && bytes != nullptr && bytes[4] == 'f' && bytes[5] == 't' &&
        bytes[6] == 'y' && bytes[7] == 'p') {
        return make_result(Format::Other, true, "MP4", "media-capability", "capability-only",
                           "EXE_CONFIRMED", "video/mp4");
    }
    if (size >= 4 && bytes != nullptr && bytes[0] == 0 && bytes[1] == 0 &&
        bytes[2] == 1 && bytes[3] == 0) {
        return make_result(Format::Other, true, "ICO", "legacy-save-ui", "research-only",
                           "HIGH_CONFIDENCE", "image/x-icon");
    }

    const auto base = lower_basename(filename);
    if (base == "dmc3.sav") {
        return make_result(Format::Other, false, "dmc3.sav", "persistence", "structural",
                           "DATA_CONFIRMED", "application/vnd.dmc.save");
    }
    if (base == "options.sav") {
        return make_result(Format::Other, false, "options.sav", "persistence", "research-only",
                           "HIGH_CONFIDENCE", "application/vnd.dmc.save");
    }
    if (base == "icon.sys") {
        return make_result(Format::Other, false, "icon.sys", "legacy-save-ui", "research-only",
                           "HIGH_CONFIDENCE", "application/octet-stream");
    }
    if (base.size() > 8 && base.rfind("eventtbl", 0) == 0 &&
        base.size() >= 4 && base.substr(base.size() - 4) == ".bin") {
        return make_result(Format::Other, false, "EventTbl", "mission-bytecode", "research-only",
                           "HIGH_CONFIDENCE", "application/octet-stream");
    }

    return from_extension(lower_extension(filename));
}

std::string describe_resource(std::string_view filename,
                              const std::uint8_t* bytes,
                              std::size_t size,
                              const ProbeResult& p) {
    if (!p.recognized) return "unknown resource";

    if (std::string_view{p.family} == "PAC") {
        return describe_slot_container(bytes, size, "PAC");
    }
    if (std::string_view{p.family} == "PNST") {
        return describe_slot_container(bytes, size, "PNST");
    }
    if (std::string_view{p.family} == "HITS") {
        std::ostringstream out;
        if (!p.content_confirmed || bytes == nullptr || size < 0x44u) {
            out << "HITS collision payload | header="
                << (size >= 0x44u ? "unconfirmed" : "truncated");
            return out.str();
        }
        const auto grid_x = read_u32_le(bytes + 0x2cu);
        const auto grid_y = read_u32_le(bytes + 0x30u);
        const auto grid_z = read_u32_le(bytes + 0x34u);
        const auto triangles = read_u32_le(bytes + 0x38u);
        const auto spatial_rel = read_u32_le(bytes + 0x3cu);
        const auto triangle_rel = read_u32_le(bytes + 0x40u);
        out << "HITS collision grid"
            << " | cells=" << grid_x << "x" << grid_y << "x" << grid_z
            << " | triangles=" << triangles
            << " | spatial_rel=0x" << std::hex << spatial_rel
            << " triangle_rel=0x" << triangle_rel << std::dec
            << " | identity=data/corpus (not EXE registry tag)";
        return out.str();
    }
    if (std::string_view{p.family} == "DCA") {
        std::ostringstream out;
        if (size < 0x10u) return "DCA truncated header";
        const auto payload = size - 0x10u;
        out << "DCA camera records | count=" << (payload / 0x410u)
            << " | record_size=0x410"
            << " | trailing=" << (payload % 0x410u);
        return out.str();
    }
    if (std::string_view{p.family} == "DDS") {
        std::ostringstream out;
        out << "DDS texture";
        if (p.content_confirmed && bytes != nullptr && size >= 20u) {
            out << " | width=" << read_u32_le(bytes + 16u)
                << " height=" << read_u32_le(bytes + 12u);
        }
        return out.str();
    }
    if (std::string_view{p.family} == "LIG2") {
        std::ostringstream out;
        if (size < 0x20u) return "LIG2 truncated 0x20 header";
        const auto payload = size - 0x20u;
        out << "LIG2 lighting records | count=" << (payload / 0x30u)
            << " | record_size=0x30"
            << " | trailing=" << (payload % 0x30u);
        return out.str();
    }
    if (std::string_view{p.family} == "LIG" && size >= 0x20u) {
        const auto payload = size - 0x20u;
        if (payload % 0x30u == 0u) {
            std::ostringstream out;
            out << "LIG resource | LIG2-compatible record envelope candidate: records="
                << (payload / 0x30u)
                << " | not promoted to LIG2 identity from extension alone";
            return out.str();
        }
    }
    if (std::string_view{p.family} == "NBZ") {
        std::ostringstream out;
        const bool zip_prefix = bytes != nullptr && size >= 4u && bytes[0] == 'P' && bytes[1] == 'K';
        out << "NBZ top-level volume | identity=filename/extension"
            << " | ZIP-prefix=" << (zip_prefix ? "yes" : "no")
            << " | binary AFS identity is not inferred";
        return out.str();
    }

    std::ostringstream out;
    out << p.family
        << " | domain=" << p.domain
        << " | support=" << p.support
        << " | evidence=" << p.evidence
        << " | identity=" << (p.content_confirmed ? "content-confirmed" : "extension/name-only")
        << " | bytes=" << size;
    if (!filename.empty()) out << " | file=" << filename;
    return out.str();
}

const char* format_name(Format format) noexcept {
    switch (format) {
        case Format::Scm: return "SCM";
        case Format::Mod: return "MOD";
        case Format::Other: return "DMC_RESOURCE";
        default: return "UNKNOWN";
    }
}

}  // namespace dmcresource
