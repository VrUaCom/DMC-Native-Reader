#include "dmcresource/native_module.h"

#include <string>
#include <vector>

namespace dmcresource {
namespace {

PipelineResult run_catalog_recognition(const NativeModule& module,
                                       std::string_view filename,
                                       const std::uint8_t* bytes,
                                       std::size_t size,
                                       const ProbeResult& probe) noexcept {
    PipelineResult out;
    out.probe = probe;
    out.accepted = true;
    out.renderable = false;
    out.detail = describe_resource(filename, bytes, size, probe);
    if (!out.detail.empty()) out.detail += "\n";
    out.detail += "Explicit family module is registered; semantic decoder is not promoted yet.";
    out.modules.push_back({"identity-probe", true});
    out.modules.push_back({"family-module-contract", true});
    out.modules.push_back({module.id, false});
    return out;
}

NativeModule recognition(const char* id, const char* family) noexcept {
    return {id, family, Format::Other, ModuleKind::Recognition,
            false, run_catalog_recognition};
}

}  // namespace

std::vector<NativeModule> catalog_recognition_modules() {
    return {
        recognition("formats.pe.recognition", "PE"),
        recognition("formats.pack.recognition", "PACK"),
        recognition("formats.lst.recognition", ".lst"),
        recognition("formats.afs-namespace.recognition", "AFS namespace"),
        recognition("formats.ukn.recognition", ".ukn"),
        recognition("formats.bin-fallback.recognition", ".bin"),

        recognition("formats.tim2.recognition", "TIM2"),
        recognition("formats.ptz.recognition", "PTZ"),

        recognition("formats.sef.recognition", "SEF"),
        recognition("formats.efe.recognition", "EFE"),
        recognition("formats.efw.recognition", "EFW"),

        recognition("formats.c1d.recognition", "C1D"),
        recognition("formats.clt.recognition", "CLT"),

        recognition("formats.mot.recognition", "MOT"),
        recognition("formats.mot2.recognition", "MOT2"),
        recognition("formats.mot3.recognition", "MOT3"),
        recognition("formats.mot4.recognition", "MOT4"),
        recognition("formats.mot5.recognition", "MOT5"),
        recognition("formats.mot6.recognition", "MOT6"),
        recognition("formats.mcv.recognition", "MCV"),
        recognition("formats.cam.recognition", "CAM"),
        recognition("formats.hid.recognition", "HID"),
        recognition("formats.hid2.recognition", "HID2"),
        recognition("formats.hid3.recognition", "HID3"),
        recognition("formats.tsc.recognition", "TSC"),

        recognition("formats.eve.recognition", "EVE"),
        recognition("formats.pos.recognition", "POS"),
        recognition("formats.itm.recognition", "ITM"),
        recognition("formats.ste.recognition", "STE"),
        recognition("formats.est.recognition", "EST"),

        recognition("formats.adx.recognition", "ADX"),
        recognition("formats.ogg.recognition", "OGG"),
        recognition("formats.vagp.recognition", "VAGp"),
        recognition("formats.phd.recognition", "PHD"),
        recognition("formats.tsb.recognition", "TSB"),
        recognition("formats.bd.recognition", "BD"),
        recognition("formats.spumapdt.recognition", "SPUMAPDT"),

        recognition("formats.sfd.recognition", "SFD"),
        recognition("formats.wmv.recognition", "WMV"),
        recognition("formats.pss.recognition", "PSS"),
        recognition("formats.thp.recognition", "THP"),
        recognition("formats.pam.recognition", "PAM"),
        recognition("formats.xmv.recognition", "XMV"),
        recognition("formats.pmf.recognition", "PMF"),
        recognition("formats.avi.recognition", "AVI"),
        recognition("formats.mpg.recognition", "MPG"),
        recognition("formats.bik.recognition", "BIK"),
        recognition("formats.mp4.recognition", "MP4"),

        recognition("formats.sav.recognition", "SAV"),
        recognition("formats.dmc3-sav.recognition", "dmc3.sav"),
        recognition("formats.options-sav.recognition", "options.sav"),
        recognition("formats.fon.recognition", "FON"),
        recognition("formats.ico.recognition", "ICO"),
        recognition("formats.icon-sys.recognition", "icon.sys"),
        recognition("formats.eventtbl.recognition", "EventTbl"),
    };
}

}  // namespace dmcresource
