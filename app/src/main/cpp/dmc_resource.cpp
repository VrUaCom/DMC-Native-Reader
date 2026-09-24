#include "dmcresource/dmc_resource.h"

#include <algorithm>
#include <cctype>
#include <span>
#include <sstream>
#include <string>
#include <string_view>

#include "dmcresource/motion/cloth_chain.h"
#include "dmcresource/motion/motion_script.h"
#include "dmcresource/ptx_framing_compat.h"
#include "dmcresource/motion/uv_scroll.h"

namespace dmcresource {
namespace {

std::string lower_extension(std::string_view filename) {
    const auto dot = filename.find_last_of('.');
    if (dot == std::string_view::npos || dot + 1U >= filename.size()) return {};
    std::string out(filename.substr(dot + 1U));
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char value) {
        return static_cast<char>(std::tolower(value));
    });
    return out;
}

bool magic4(const std::uint8_t* bytes, std::size_t size,
            char a, char b, char c, char d) noexcept {
    return bytes != nullptr && size >= 4U &&
           bytes[0] == static_cast<std::uint8_t>(a) &&
           bytes[1] == static_cast<std::uint8_t>(b) &&
           bytes[2] == static_cast<std::uint8_t>(c) &&
           bytes[3] == static_cast<std::uint8_t>(d);
}

ProbeResult result(Format format,
                   bool content_confirmed,
                   const char* family,
                   const char* domain,
                   const char* support,
                   const char* evidence,
                   const char* mime) noexcept {
    return {format, true, content_confirmed, family, domain, support, evidence, mime};
}

}  // namespace

ProbeResult probe(std::string_view filename,
                  const std::uint8_t* bytes,
                  std::size_t size) {
    // Strong byte identities always win over file names.
    if (magic4(bytes, size, 'S', 'C', 'M', ' ')) {
        return result(Format::Scm, true, "SCM", "geometry", "render-scene",
                      "EXE_AND_CORPUS_CONFIRMED", "application/vnd.dmc.scm");
    }
    if (magic4(bytes, size, 'M', 'O', 'D', ' ')) {
        return result(Format::Mod, true, "MOD", "geometry", "render-scene",
                      "EXE_AND_CORPUS_CONFIRMED", "application/vnd.dmc.mod");
    }
    // EFM effect models: MOD document layout plus COLOR0 (0x1402F7A90).
    if (magic4(bytes, size, 'E', 'F', 'M', ' ')) {
        return result(Format::Mod, true, "EFM", "geometry", "render-scene",
                      "EXE_CONFIRMED", "application/vnd.dmc.efm");
    }
    if (magic4(bytes, size, 'D', 'D', 'S', ' ')) {
        return result(Format::Dds, true, "DDS", "texture", "image-preview",
                      "DATA_CONFIRMED", "image/vnd-ms.dds");
    }
    if (magic4(bytes, size, 'E', 'V', 'T', '\0')) {
        // Stock DMC3 paths are eventtbl\\EventTblNN.bin. "EVT" is the content
        // magic/family tag, not evidence for a stock .evt filename extension.
        return result(Format::Evt, true, "EventTbl", "event-script", "inspection",
                      "STRUCTURAL_CONFIRMED", "application/vnd.dmc.eventtbl");
    }

    if (magic4(bytes, size, 'P', 'A', 'C', '\0')) {
        return result(Format::Pac, true, "PAC", "archive", "child-resources",
                      "EXE_AND_CORPUS_CONFIRMED", "application/vnd.dmc.pac");
    }
    // PNST: same relative-slot layout as PAC (weapon archives obj\\plwp_*.pac
    // use it despite the .pac name).
    if (magic4(bytes, size, 'P', 'N', 'S', 'T')) {
        return result(Format::Pnst, true, "PNST", "archive", "child-resources",
                      "STRUCTURAL_CONFIRMED", "application/vnd.dmc.pac");
    }
    // SHW shadow hulls (runtime builder 0x14031FD30, per-frame 0x1403200D0).
    if (magic4(bytes, size, 'S', 'H', 'W', ' ')) {
        return result(Format::Shw, true, "SHW", "shadow", "render-scene",
                      "EXE_AND_CORPUS_CONFIRMED", "application/vnd.dmc.shw");
    }
    // Text scripts: .tsc texture scroll (".TSC" first token, 0x14030A9B0) and
    // .clt chain parameters (";name.clt" + ClothNo, 0x1402CA345).
    if (bytes != nullptr && size > 0U) {
        const std::string_view text{reinterpret_cast<const char*>(bytes), size};
        if (motion::looks_like_tsc(text)) {
            return result(Format::Tsc, true, "TSC", "texture-scroll", "inspection",
                          "EXE_CONFIRMED", "text/vnd.dmc.tsc");
        }
        if (motion::looks_like_clt(text)) {
            return result(Format::Clt, true, "CLT", "cloth", "inspection",
                          "EXE_CONFIRMED", "text/vnd.dmc.clt");
        }
    }
    // MOT keeps its identity at +0x04 after the u32 header size.
    if (bytes != nullptr && size >= 8U && magic4(bytes + 4U, size - 4U, 'M', 'O', 'T', '\0')) {
        return result(Format::Mot, true, "MOT", "animation", "inspection",
                      "EXE_AND_CORPUS_CONFIRMED", "application/vnd.dmc.mot");
    }

    // PTX and descriptor-wrapped textures have no standalone four-byte identity
    // gate. Extensions are routing candidates only; the texture module validates
    // bytes before accepting them. DMC3 HD also keeps legacy .tm2 logical names
    // whose physical bytes are DMC descriptor + DDS rather than Sony TIM2.
    //
    // EventTbl intentionally has NO extension fallback here. The canonical EXE
    // requests EventTblNN.bin, while .bin is a generic leaf extension shared by
    // unrelated payloads. EventTbl identity therefore requires EVT\0 bytes.
    const auto extension = lower_extension(filename);
    if (extension == "scm") {
        return result(Format::Scm, false, "SCM", "geometry", "render-scene",
                      "EXE_CONFIRMED", "application/vnd.dmc.scm");
    }
    if (extension == "mod") {
        return result(Format::Mod, false, "MOD", "geometry", "render-scene",
                      "EXE_CONFIRMED", "application/vnd.dmc.mod");
    }
    if (extension == "dds" || extension == "tm2") {
        return result(Format::Dds, false, "DDS", "texture", "image-preview",
                      "STRUCTURAL_CONFIRMED", "image/vnd-ms.dds");
    }
    if (extension == "ptx") {
        return result(Format::Ptx, false, "PTX", "texture", "child-resources",
                      "STRUCTURAL_CONFIRMED", "application/vnd.dmc.ptx");
    }
    // Player motion script (pl000.pac slot 5, loader 0x1400594B0): no magic,
    // identified by its bank tables (last, after every magic and extension).
    if (bytes != nullptr &&
        motion::MotionScriptFile::looks_like(std::span<const std::uint8_t>{bytes, size})) {
        return result(Format::MotionScript, true, "MotionScript", "motion-script", "inspection",
                      "EXE_CONFIRMED", "application/vnd.dmc.motion-script");
    }
    // Descriptor texture bundles saved under another name (a PAC slot dumped
    // as .bin): the same byte validation the PAC classifier uses.
    if (bytes != nullptr && size != 0U) {
        const auto parsed = ptx_compat::parse_texture_bundle(
            std::span<const std::byte>{reinterpret_cast<const std::byte*>(bytes), size});
        if (parsed.ok()) {
            return result(Format::Ptx, true, "PTX", "texture", "child-resources",
                          "STRUCTURAL_CONFIRMED", "application/vnd.dmc.ptx");
        }
    }
    return {};
}

std::string describe_resource(std::string_view filename,
                              const std::uint8_t*,
                              std::size_t size,
                              const ProbeResult& probe_result) {
    std::ostringstream out;
    out << probe_result.family
        << " | bytes=" << size
        << " | source=" << filename
        << " | identity="
        << (probe_result.content_confirmed ? "content-confirmed" : "routing-candidate");
    return out.str();
}

const char* format_name(Format format) noexcept {
    switch (format) {
    case Format::Scm: return "SCM";
    case Format::Mod: return "MOD";
    case Format::Dds: return "DDS";
    case Format::Ptx: return "PTX";
    case Format::Evt: return "EventTbl";
    case Format::Pac: return "PAC";
    case Format::Mot: return "MOT";
    case Format::Pnst: return "PNST";
    case Format::Shw: return "SHW";
    case Format::Tsc: return "TSC";
    case Format::Clt: return "CLT";
    case Format::MotionScript: return "MotionScript";
    case Format::Unknown: return "UNKNOWN";
    }
    return "UNKNOWN";
}

}  // namespace dmcresource
