#include <array>
#include <cassert>
#include <cstdint>
#include <string_view>

#include "dmcresource/dmc_resource.h"
#include "dmcresource/native_module.h"

int main() {
    using namespace dmcresource;

    const auto& modules = NativeModuleRegistry::modules();
    assert(modules.size() == 11U);

    const auto* scm = NativeModuleRegistry::find("SCM");
    const auto* mod = NativeModuleRegistry::find("MOD");
    const auto* dds = NativeModuleRegistry::find("DDS");
    const auto* ptx = NativeModuleRegistry::find("PTX");
    const auto* event_tbl = NativeModuleRegistry::find("EventTbl");
    assert(scm != nullptr && scm->format == Format::Scm && scm->renderable);
    assert(mod != nullptr && mod->format == Format::Mod && mod->renderable);
    assert(dds != nullptr && dds->format == Format::Dds && !dds->renderable);
    assert(ptx != nullptr && ptx->format == Format::Ptx && !ptx->renderable);
    assert(event_tbl != nullptr && event_tbl->format == Format::Evt &&
           !event_tbl->renderable);

    // v34: PAC (read-only archive browser/assembler) and MOT (motion reader)
    // are promoted; both are byte-identified, never by extension alone.
    const auto* pac = NativeModuleRegistry::find("PAC");
    const auto* mot = NativeModuleRegistry::find("MOT");
    assert(pac != nullptr && pac->format == Format::Pac && !pac->renderable);
    assert(mot != nullptr && mot->format == Format::Mot && !mot->renderable);
    const std::array<std::uint8_t, 8> pac_magic{'P', 'A', 'C', 0U, 0U, 0U, 0U, 0U};
    const std::array<std::uint8_t, 8> mot_magic{0x30U, 0U, 0U, 0U, 'M', 'O', 'T', 0U};
    assert(probe("renamed.bin", pac_magic.data(), pac_magic.size()).format == Format::Pac);
    assert(probe("renamed.bin", mot_magic.data(), mot_magic.size()).format == Format::Mot);
    assert(!probe("motion.mot", nullptr, 0U).recognized);

    // PNST (weapon archives obj\\plwp_*.pac) shares the relative-slot layout.
    const auto* pnst = NativeModuleRegistry::find("PNST");
    assert(pnst != nullptr && pnst->format == Format::Pnst && !pnst->renderable);
    assert(pnst->run == pac->run);
    const std::array<std::uint8_t, 8> pnst_magic{'P', 'N', 'S', 'T', 0U, 0U, 0U, 0U};
    assert(probe("plwp_sword.pac", pnst_magic.data(), pnst_magic.size()).format ==
           Format::Pnst);

    // SHW shadow hulls: content-confirmed by magic, rendered as hull geometry.
    const auto* shw = NativeModuleRegistry::find("SHW");
    assert(shw != nullptr && shw->format == Format::Shw && shw->renderable);
    const std::array<std::uint8_t, 4> shw_magic{'S', 'H', 'W', ' '};
    assert(probe("renamed.bin", shw_magic.data(), shw_magic.size()).format == Format::Shw);

    // TSC / CLT text scripts: identified by content (".TSC" first token,
    // ";name.clt" + ClothNo), inspection only.
    const auto* tsc = NativeModuleRegistry::find("TSC");
    const auto* clt = NativeModuleRegistry::find("CLT");
    assert(tsc != nullptr && tsc->format == Format::Tsc && !tsc->renderable);
    assert(clt != nullptr && clt->format == Format::Clt && !clt->renderable);
    constexpr std::string_view tsc_text = "\r\n.TSC\t\n\t# RELATIVE\n<Finish>\n$";
    constexpr std::string_view clt_text = ";a.clt\nClothNum 1\nClothNo 0\nBone 2 Y\nEnd\n$";
    assert(probe("renamed.bin", reinterpret_cast<const std::uint8_t*>(tsc_text.data()),
                 tsc_text.size()).format == Format::Tsc);
    assert(probe("renamed.bin", reinterpret_cast<const std::uint8_t*>(clt_text.data()),
                 clt_text.size()).format == Format::Clt);

    // Removed/archived families must not leak back into the clean registry.
    for (const std::string_view family : {
             "HITS", "TXT", ".index", "DCA", "LIG", "LIG2",
             "NBZ", "EFM", "MRP"}) {
        assert(NativeModuleRegistry::find(family) == nullptr);
    }

    const std::array<std::uint8_t, 4> scm_magic{'S', 'C', 'M', ' '};
    const std::array<std::uint8_t, 4> mod_magic{'M', 'O', 'D', ' '};
    const std::array<std::uint8_t, 4> dds_magic{'D', 'D', 'S', ' '};
    const std::array<std::uint8_t, 4> evt_magic{'E', 'V', 'T', 0U};
    assert(probe("renamed.bin", scm_magic.data(), scm_magic.size()).format == Format::Scm);
    assert(probe("renamed.bin", mod_magic.data(), mod_magic.size()).format == Format::Mod);
    assert(probe("renamed.bin", dds_magic.data(), dds_magic.size()).format == Format::Dds);
    assert(probe("renamed.bin", evt_magic.data(), evt_magic.size()).format == Format::Evt);
    assert(probe("texture.ptx", nullptr, 0U).format == Format::Ptx);
    assert(probe("legacy.tm2", nullptr, 0U).format == Format::Dds);

    assert(!probe("stage.hits", nullptr, 0U).recognized);
    assert(!probe("stage.dca", nullptr, 0U).recognized);
    assert(!probe("stage.pac", nullptr, 0U).recognized);
    assert(!probe("stage.txt", nullptr, 0U).recognized);
    assert(!probe("model.shw", nullptr, 0U).recognized);

    return 0;
}
