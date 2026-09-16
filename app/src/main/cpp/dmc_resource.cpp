#include "dmcresource/dmc_resource.h"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <string>
#include <string_view>

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
    case Format::Unknown: return "UNKNOWN";
    }
    return "UNKNOWN";
}

}  // namespace dmcresource
