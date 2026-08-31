#include "dmcresource/exe_format_registry.h"

#include <array>
#include <cctype>
#include <sstream>
#include <string>

namespace dmcresource {
namespace {

struct Record {
    const char* family;
    const char* strength;
    const char* path;
    const char* detail;
};

// Canonical artifact:
// SHA-256 e454272ed0fb0247fcbcf300e5d55d7a3e96d50b89b9ffaff81bb978dcbdd082
// size 6,356,432; PE32+ x86-64; ImageBase 0x140000000.
//
// IMPORTANT: this table records identity/reference evidence only.  It does not
// promote a family to a complete decoder.  Shader-container strings such as
// RDEF/SPDB/D3DSHDR/SHEX are deliberately excluded because they are embedded
// compiler metadata, not DMC resource-family identities.
constexpr std::array<Record, 46> kRecords{{
    {"MOD", "EXE_CONFIRMED", "registry-content + container-dispatch + family-mask",
     "3-byte registry @0x1402DB1F0; handler 0x1402FE3B0; 4-byte family mask @0x1402FD650"},
    {"EFM", "EXE_CONFIRMED", "registry-content + container-dispatch + family-mask",
     "3-byte registry @0x1402DB1F0; handler 0x1402F7A90; 4-byte family mask @0x1402FD650"},
    {"SCM", "EXE_CONFIRMED", "registry-content + container-dispatch + family-mask",
     "3-byte registry @0x1402DB1F0; handler 0x1403051B0; 4-byte family mask @0x1402FD650"},
    {"MRP", "EXE_CONFIRMED_IDENTITY", "registry-content + family-mask",
     "type 3 in 3-byte registry; 0x40000000 in family mask; no generic handler proven"},
    {"SHW", "EXE_CONFIRMED", "registry-content + container-dispatch + family-mask",
     "3-byte registry; handler 0x1403204C0; 0x60000000 family mask"},
    {"MCV", "EXE_CONFIRMED_IDENTITY", "family-mask + auxiliary extension registry",
     "MCV<space> -> 0x50000000 @0x1402FD650; .mcv/.MCV classified @0x1402E01A0"},
    {"EFE", "EXE_CONFIRMED_IDENTITY", "container-dispatch sentinel",
     "3-byte EFE is explicitly compared by 0x1401B9FA0 and follows no-handler path"},
    {"EFW", "EXE_CONFIRMED_IDENTITY", "container-dispatch sentinel",
     "3-byte EFW is explicitly compared by 0x1401B9FA0 and follows no-handler path"},
    {"PNST", "EXE_CONFIRMED", "container-recursion magic",
     "4-byte PNST identity recursively dispatched by 0x1401B9FA0"},
    {"PTX", "EXE_CONFIRMED_IDENTITY", "filename extension classifier",
     ".ptx/.PTX/.Ptx checked by 0x1402DB3C0; assigned local class 4"},
    {"CLT", "EXE_CONFIRMED_IDENTITY", "two filename extension classifiers",
     ".clt/.CLT/.Clt checked by 0x1402DB3C0 and .clt/.CLT by 0x1402E01A0"},
    {"C1D", "EXE_CONFIRMED_IDENTITY", "filename extension classifier",
     ".c1d case variants checked by 0x1402DB3C0; assigned local class 6"},
    {"MOT", "EXE_CONFIRMED_IDENTITY", "auxiliary extension registry",
     "?.mot/.MOT checks in 0x1402E01A0; local class 0"},
    {"CAM", "EXE_CONFIRMED_IDENTITY", "auxiliary extension registry",
     ".cam/.CAM checks in 0x1402E01A0; local class 2"},
    {"HID", "EXE_CONFIRMED_IDENTITY", "auxiliary extension registry",
     ".hid/.HID checks in 0x1402E01A0; local class 3"},
    {"TSC", "EXE_CONFIRMED_IDENTITY", "auxiliary extension registry",
     ".tsc/.TSC checks in 0x1402E01A0; local class 5"},

    {"PSS", "EXE_CONFIRMED_CAPABILITY", "media extension classifier", ".PSS literal compared near 0x14002A5D1"},
    {"THP", "EXE_CONFIRMED_CAPABILITY", "media extension classifier", ".THP literal compared near 0x14002A604"},
    {"PAM", "EXE_CONFIRMED_CAPABILITY", "media extension classifier", ".PAM literal compared near 0x14002A634"},
    {"XMV", "EXE_CONFIRMED_CAPABILITY", "media extension classifier", ".XMV literal compared near 0x14002A664"},
    {"WMV", "EXE_CONFIRMED_CAPABILITY", "media extension classifier + runtime string", ".WMV literal compared near 0x14002A694"},
    {"PMF", "EXE_CONFIRMED_CAPABILITY", "media extension classifier", ".PMF literal compared near 0x14002A6C4"},
    {"AVI", "EXE_CONFIRMED_CAPABILITY", "media extension classifier", ".AVI literal compared near 0x14002A6F4"},
    {"MPG", "EXE_CONFIRMED_CAPABILITY", "media extension classifier", ".MPG literal compared near 0x14002A723"},
    {"BIK", "EXE_CONFIRMED_CAPABILITY", "media extension classifier", ".BIK literal compared near 0x14002A751"},
    {"MP4", "EXE_CONFIRMED_CAPABILITY", "media extension classifier", ".MP4 literal compared at classifier tail near 0x14002A779"},

    {"NBZ", "EXE_PATH_CONFIRMED", "runtime path string", "%sDMC3-%d.nbz at VA 0x14036E930"},
    {"AFS", "EXE_PATH_CONFIRMED", "runtime namespace/path strings", "GData.afs/ at VA 0x140363188; GDataX360.afs/ also present"},
    {"PAC", "EXE_PATH_CONFIRMED", "large runtime path corpus", "thousands of .pac resource-name strings embedded in canonical EXE"},
    {"ADX", "EXE_PATH_CONFIRMED", "runtime filename corpus", "hundreds of .adx filenames embedded in canonical EXE"},
    {"OGG", "EXE_PATH_CONFIRMED", "runtime filename corpus", "hundreds of .ogg filenames embedded in canonical EXE"},
    {"SFD", "EXE_PATH_CONFIRMED", "runtime filename corpus", "mission/cutscene .sfd filenames embedded in canonical EXE"},
    {"TM2", "EXE_PATH_CONFIRMED", "runtime filename corpus", "multiple .tm2 texture filenames embedded in canonical EXE"},
    {"PTZ", "EXE_PATH_CONFIRMED", "runtime filename string", "basic.ptz embedded in canonical EXE"},
    {"DDS", "EXE_PATH_CONFIRMED", "runtime filename string", "LOADERICON.dds embedded in canonical EXE"},
    {"FON", "EXE_PATH_CONFIRMED", "runtime filename corpus", "font/*.fon names embedded in canonical EXE"},
    {"ICO", "EXE_PATH_CONFIRMED", "runtime filename corpus", "icon00.ico/icon01.ico/icon02.ico embedded in canonical EXE"},
    {"SYS", "EXE_PATH_CONFIRMED", "runtime filename string", "icon.sys embedded in canonical EXE"},
    {"SAV", "EXE_PATH_CONFIRMED", "runtime filename strings", "options.sav and dmc3.sav embedded in canonical EXE"},
    {"PHD", "EXE_PATH_CONFIRMED", "runtime filename string", "snd_sys.phd embedded in canonical EXE"},
    {"TSB", "EXE_PATH_CONFIRMED", "runtime filename string", "snd_sys.tsb embedded in canonical EXE"},
    {"BD", "EXE_PATH_CONFIRMED", "runtime filename string", "snd_sys.bd embedded in canonical EXE"},
    {"BIN", "EXE_PATH_CONFIRMED", "runtime filename corpus", "SpuMap.bin and EventTblNN.bin families embedded in canonical EXE"},
    {"TXT", "EXE_PATH_CONFIRMED", "runtime filename corpus", "hundreds of message/config .txt names embedded in canonical EXE"},
    {"SPUMAPDT", "EXE_STRING_CONFIRMED", "explicit executable string", "SPUMAPDT string at VA 0x140508988; semantic ownership remains separate"},
    {"PE", "HOST_ARTIFACT_CONFIRMED", "canonical executable", "dmc3.exe itself is PE32+ x86-64; this is host identity, not a game-resource decoder claim"},
}};

std::string normalize(std::string_view input) {
    std::string out;
    out.reserve(input.size());
    for (char ch : input) {
        const unsigned char c = static_cast<unsigned char>(ch);
        if (std::isalnum(c)) out.push_back(static_cast<char>(std::toupper(c)));
    }

    if (out == "AFSNAMESPACE") return "AFS";
    if (out == "TIM2") return "TM2";
    if (out == "ICONSYS") return "SYS";
    if (out == "DMC3SAV" || out == "OPTIONSSAV") return "SAV";
    if (out == "EVENTTBL") return "BIN";
    if (out == "DMCRESOURCE") return {};
    return out;
}

}  // namespace

ExeFormatEvidence exe_format_evidence(std::string_view family) noexcept {
    const std::string key = normalize(family);
    if (key.empty()) return {};
    for (const auto& record : kRecords) {
        if (key == record.family) {
            return {true, record.strength, record.path, record.detail};
        }
    }
    return {};
}

std::string describe_exe_format_evidence(std::string_view family) {
    const auto evidence = exe_format_evidence(family);
    if (!evidence.present) return {};
    std::ostringstream out;
    out << "EXE evidence=" << evidence.strength
        << " | path=" << evidence.path
        << " | " << evidence.detail;
    return out.str();
}

}  // namespace dmcresource
