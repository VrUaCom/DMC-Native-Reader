#include "dmcresource/dmc_resource.h"
#include "dmcresource/texture_reencode.h"

#include <algorithm>
#include <cctype>
#include <span>
#include <sstream>
#include <cstring>
#include <string>
#include <string_view>

#include "dmcresource/collision_shapes.h"
#include "dmcresource/effect_bank.h"
#include "dmcresource/font.h"
#include "dmcresource/stage_config.h"
#include "dmcresource/sound.h"
#include "dmc_rengine/formats/so/graph.hpp"
#include "dmc_rengine/formats/stage_txt.hpp"
#include "dmcresource/motion/cloth_chain.h"
#include "dmcresource/motion/motion_script.h"
#include "dmcresource/ptx_framing_compat.h"
#include "dmcresource/motion/uv_scroll.h"

namespace dmcresource {
namespace {

// Stage room setup text: clean lexing and at least one stage directive.
[[nodiscard]] bool looks_like_stage_text(std::span<const std::uint8_t> bytes) {
    if (bytes.size() > (4U << 20U)) return false;
    const auto lex = dmc::rengine::formats::stage_txt::Lexer::scan(std::as_bytes(bytes));
    if (!lex.ok()) return false;
    using dmc::rengine::formats::stage_txt::TokenKind;
    for (const auto kind : {TokenKind::directive_set, TokenKind::door, TokenKind::box_in, TokenKind::next_room}) {
        if (!lex.by_kind(kind).empty()) return true;
    }
    return false;
}

// A slot name list: an optional PNST line, then file names (a stem, a dot,
// an extension of 2-5 letters or digits), one a line; NUL only as padding.
[[nodiscard]] bool looks_like_name_index(std::string_view text) {
    if (text.size() > (1U << 20U)) return false;
    while (!text.empty() && text.back() == '\0') text.remove_suffix(1U);
    std::size_t names = 0U, at = 0U;
    bool first = true;
    while (at < text.size()) {
        auto end = text.find('\n', at);
        if (end == std::string_view::npos) end = text.size();
        auto line = text.substr(at, end - at);
        at = end + 1U;
        while (!line.empty() && line.back() == '\r') line.remove_suffix(1U);
        if (line.empty()) continue;
        if (first && line == "PNST") { first = false; continue; }
        first = false;
        const auto dot = line.rfind('.');
        if (dot == std::string_view::npos || dot == 0U || line.size() - dot - 1U < 2U || line.size() - dot - 1U > 5U) {
            return false;
        }
        for (const char c : line) {
            const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                            c == '_' || c == '-' || c == '.' || c == '\\' || c == '/';
            if (!ok) return false;
        }
        ++names;
    }
    return names >= 1U;
}


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
    if (magic4(bytes, size, 'H', 'I', 'T', 'S')) {
        return result(Format::Hits, true, "HITS", "environment-collision", "inspection",
                      "EXE_AND_CORPUS_CONFIRMED", "application/vnd.dmc.hits");
    }

    if (magic4(bytes, size, 'P', 'A', 'C', '\0')) {
        return result(Format::Pac, true, "PAC", "archive", "child-resources",
                      "EXE_AND_CORPUS_CONFIRMED", "application/vnd.dmc.pac");
    }
    // PNST: same relative-slot layout as PAC (weapon archives obj\\plwp_*.pac
    // use it despite the .pac name).
    // Effect bank (loader 0x1402C04C0): a PNST whose slot 0 is the manifest.
    // A slot name list may open with a "PNST" line; it is text, not the
    // container (whose +4 is a binary slot count).
    if (magic4(bytes, size, 'P', 'N', 'S', 'T') &&
        looks_like_name_index(std::string_view{reinterpret_cast<const char*>(bytes), size})) {
        return result(Format::NameIndex, true, ".index", "name-list", "inspection",
                      "STRUCTURAL_CONFIRMED", "text/vnd.dmc.index");
    }
    if (magic4(bytes, size, 'P', 'N', 'S', 'T') &&
        effect_bank::looks_like_bank(std::span<const std::uint8_t>{bytes, size})) {
        return result(Format::EffectBank, true, "FXBANK", "effect", "child-resources",
                      "EXE_CONFIRMED", "application/vnd.dmc.fxbank");
    }
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
        // Stage room setup text: lexes cleanly and carries #SET / DOOR /
        // BOXIN / NEXTROOM; a slot name list (.index): file names, one a line.
        if (looks_like_stage_text(std::span<const std::uint8_t>{bytes, size})) {
            return result(Format::StageText, true, "TXT", "stage-text", "inspection",
                          "STRUCTURAL_CONFIRMED", "text/vnd.dmc.stage");
        }
        if (looks_like_name_index(text)) {
            return result(Format::NameIndex, true, ".index", "name-list", "inspection",
                          "STRUCTURAL_CONFIRMED", "text/vnd.dmc.index");
        }
    }
    // MOT keeps its identity at +0x04 after the u32 header size.
    if (bytes != nullptr && size >= 8U && magic4(bytes + 4U, size - 4U, 'M', 'O', 'T', '\0')) {
        return result(Format::Mot, true, "MOT", "animation", "inspection",
                      "EXE_AND_CORPUS_CONFIRMED", "application/vnd.dmc.mot");
    }

    // Tagged stage and enemy families (stage corpus reverse notes).
    if (magic4(bytes, size, 'L', 'I', 'G', '2')) {
        return result(Format::Lig2, true, "LIG2", "lighting", "inspection",
                      "STRUCTURAL_CONFIRMED", "application/vnd.dmc.lig2");
    }
    if (magic4(bytes, size, 'D', 'C', 'A', '\0')) {
        return result(Format::Dca, true, "DCA", "stage-records", "inspection",
                      "STRUCTURAL_CONFIRMED", "application/vnd.dmc.dca");
    }
    if (magic4(bytes, size, 'd', 'b', 's', 'T')) {
        return result(Format::SoundTable, true, "dbsT", "sound", "inspection",
                      "STRUCTURAL_CONFIRMED", "application/vnd.dmc.tsb");
    }
    if (magic4(bytes, size, 'f', 'f', '&', 'A')) {
        return result(Format::FloatTable, true, "ff&A", "table", "inspection",
                      "STRUCTURAL_CONFIRMED", "application/vnd.dmc.ff");
    }
    if (bytes != nullptr && sound::looks_like_vag(std::span<const std::uint8_t>{bytes, size})) {
        return result(Format::Vag, true, "VAG", "sound", "image-preview",
                      "STRUCTURAL_CONFIRMED", "audio/vnd.sony.vag");
    }
    if (bytes != nullptr && size >= 8U && std::memcmp(bytes, "SPUMAPDT", 8U) == 0) {
        return result(Format::SpuMap, true, "SPUMAPDT", "sound", "inspection",
                      "STRUCTURAL_CONFIRMED", "application/vnd.dmc.spumap");
    }
    if (bytes != nullptr && size >= 8U && magic4(bytes + 4U, size - 4U, 'H', 'I', 'D', '\0')) {
        return result(Format::Hid, true, "HID", "dispatch", "inspection",
                      "EXE_CONFIRMED", "application/vnd.dmc.hid");
    }

    // Stage configuration members: a three-letter tag and a zero byte (POS,
    // EVE, CAM, ITM, STE, EST, SEF); the module reads the rest from the bytes.
    if (stage_config::Kind kind{};
        bytes != nullptr && stage_config::identify(std::span<const std::uint8_t>{bytes, size}, &kind)) {
        static constexpr const char* kMime[] = {
            "application/vnd.dmc.pos", "application/vnd.dmc.eve", "application/vnd.dmc.cam",
            "application/vnd.dmc.itm", "application/vnd.dmc.ste", "application/vnd.dmc.est",
            "application/vnd.dmc.sef"};
        static constexpr Format kFormat[] = {Format::StagePos, Format::StageEve, Format::StageCam,
                                             Format::StageItm, Format::StageSte, Format::StageEst,
                                             Format::StageSef};
        const auto index = static_cast<std::size_t>(kind);
        return result(kFormat[index], true, stage_config::kind_name(kind), "stage-config", "image-preview",
                      "STRUCTURAL_CONFIRMED", kMime[index]);
    }

    // FON bitmap fonts: no tag; the layout closing exactly is the identity
    // (pages 1..P, glyph numbers 1..G, then whole glyphs of one size).
    if (bytes != nullptr && font::looks_like_font(std::span<const std::uint8_t>{bytes, size})) {
        return result(Format::Fon, true, "FON", "font", "image-preview",
                      "STRUCTURAL_CONFIRMED", "application/vnd.dmc.fon");
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
    // Attack index tables have no content identity of their own (4-byte
    // entries); a PAC names them .colidx when the next slot is their shape
    // table, and the module validates the entries.
    if (extension == "colidx") {
        return result(Format::AttackIndex, false, "COLINDEX", "collision", "inspection",
                      "EXE_CONFIRMED", "application/vnd.dmc.colidx");
    }
    if (extension == "ptx") {
        return result(Format::Ptx, false, "PTX", "texture", "child-resources",
                      "STRUCTURAL_CONFIRMED", "application/vnd.dmc.ptx");
    }
    // One gfxTexture + DDS without a bundle header (.tm2 under another name).
    if (bytes != nullptr && size != 0U &&
        texture_reencode::is_wrapped_texture(
            std::span<const std::byte>{reinterpret_cast<const std::byte*>(bytes), size})) {
        return result(Format::Dds, true, "DDS", "texture", "image-preview",
                      "STRUCTURAL_CONFIRMED", "image/vnd-ms.dds");
    }
    // Collision shape tables (ICollisionHandle, 0x14005C260): 80-byte records.
    if (bytes != nullptr &&
        collision::looks_like_shape_table(std::span<const std::uint8_t>{bytes, size})) {
        return result(Format::CollisionShapes, true, "COLSHAPE", "collision", "inspection",
                      "EXE_CONFIRMED", "application/vnd.dmc.colshape");
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
    // SO graphs carry no tag; the reader's own shape check identifies them,
    // last of all so that every tag and extension route goes first.
    if (bytes != nullptr && size > 0U) {
        const auto raw = std::as_bytes(std::span<const std::uint8_t>{bytes, size});
        // The SO "volume table" and "link table" of dmc-rengine-cpp are the
        // collision shape tables and attack indices read above (COLSHAPE,
        // COLINDEX, from the EXE); only the graph is its own family.
        if (dmc::rengine::formats::so::graph::recognizes(raw)) {
            return result(Format::SoGraph, true, "SO-GRAPH", "graph", "inspection",
                          "CORPUS_CONFIRMED", "application/vnd.dmc.so");
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
    case Format::Hits: return "HITS";
    case Format::Pac: return "PAC";
    case Format::Mot: return "MOT";
    case Format::Pnst: return "PNST";
    case Format::Shw: return "SHW";
    case Format::Tsc: return "TSC";
    case Format::Clt: return "CLT";
    case Format::MotionScript: return "MotionScript";
    case Format::CollisionShapes: return "COLSHAPE";
    case Format::AttackIndex: return "COLINDEX";
    case Format::EffectBank: return "FXBANK";
    case Format::Fon: return "FON";
    case Format::StagePos: return "POS";
    case Format::StageEve: return "EVE";
    case Format::StageCam: return "CAM";
    case Format::StageItm: return "ITM";
    case Format::StageSte: return "STE";
    case Format::StageEst: return "EST";
    case Format::StageSef: return "SEF";
    case Format::Lig2: return "LIG2";
    case Format::Dca: return "DCA";
    case Format::StageText: return "TXT";
    case Format::NameIndex: return ".index";
    case Format::SoGraph: return "SO-GRAPH";
    case Format::SoundTable: return "dbsT";
    case Format::FloatTable: return "ff&A";
    case Format::Hid: return "HID";
    case Format::SpuMap: return "SPUMAPDT";
    case Format::Vag: return "VAG";
    case Format::Unknown: return "UNKNOWN";
    }
    return "UNKNOWN";
}

}  // namespace dmcresource
