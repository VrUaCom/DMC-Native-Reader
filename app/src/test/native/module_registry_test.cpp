#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

#include "dmcresource/dmc_resource.h"
#include "dmcresource/native_module.h"
#include "dmcresource/spider/app_info.h"

int main() {
    using namespace dmcresource;

    const auto& modules = NativeModuleRegistry::modules();
    assert(modules.size() == 46U);

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

    // Text no other module claims: message tables and plain text.
    const auto* msg = NativeModuleRegistry::find("MSG");
    const auto* text_module = NativeModuleRegistry::find("TEXT");
    assert(msg != nullptr && msg->format == Format::MessageTable);
    assert(text_module != nullptr && text_module->format == Format::PlainText);
    const std::string table = "# Msg 20600\n\t\"13th Avenue\"\n# End\n";
    assert(probe("x.bin", reinterpret_cast<const std::uint8_t*>(table.data()), table.size()).format ==
           Format::MessageTable);
    const std::string light = "Force   1400.0\r\nRange   10000.0\r\nRgb     311  280  245\r\n";
    std::vector<std::uint8_t> padded(light.begin(), light.end());
    padded.resize(64U, 0U);
    assert(probe("x.bin", padded.data(), padded.size()).format == Format::PlainText);
    // PS2 IPU picture streams play as movies.
    const std::uint8_t ipu[16] = {'i', 'p', 'u', 'm', 0, 0, 1, 0, 0, 1, 0, 1, 0x44, 0, 0, 0};
    assert(probe("x.bin", ipu, sizeof ipu).format == Format::Ipu);
    assert(NativeModuleRegistry::find("IPU") != nullptr && NativeModuleRegistry::find("IPU")->format == Format::Ipu);
    // The sound bank header beside a VAG body (snd_*.pac slot 1).
    std::vector<std::uint8_t> hd(0x50U, 0U);
    const auto put32 = [&hd](std::size_t at, std::uint32_t v) {
        for (int i = 0; i < 4; ++i) hd[at + i] = static_cast<std::uint8_t>(v >> (8 * i));
    };
    std::memcpy(hd.data(), "Head", 4); put32(4, 0x20); put32(8, 1); put32(0x0C, 0x50); put32(0x10, 0x40);
    put32(0x14, 0x20); put32(0x18, 0x30); put32(0x1C, 0x40);
    std::memcpy(hd.data() + 0x20, "Prog", 4); put32(0x24, 0x10);
    std::memcpy(hd.data() + 0x30, "Smpl", 4); put32(0x34, 0x10);
    std::memcpy(hd.data() + 0x40, "Vagi", 4); put32(0x44, 0x10);
    assert(probe("x.bin", hd.data(), hd.size()).format == Format::SoundHeader);
    // An object list with Shift-JIS comments (st412 slot 4) is text too.
    std::string objects = "# GAME\r\n\r\n# SET 0 STAY\t;";
    for (int i = 0; i < 3; ++i) objects += "\x82\xa0\x82\xa2";
    objects += "\r\n\tmodel\t\t0\r\n\tpos\t\t\t0 0 0\r\n\thit\tbox\t\t2566, 90, -500, 2000, 2000, 2000\r\n";
    assert(probe("x.bin", reinterpret_cast<const std::uint8_t*>(objects.data()), objects.size()).format ==
           Format::PlainText);
    const std::uint8_t binary[8] = {1, 0, 0, 0, 2, 0, 0, 0};
    assert(probe("x.bin", binary, sizeof binary).format != Format::PlainText);

    const auto* hits = NativeModuleRegistry::find("HITS");
    assert(hits != nullptr && hits->format == Format::Hits && hits->renderable &&
           has_capability(hits->capabilities, ResourceCapability::Collision));
    const std::array<std::uint8_t, 4> hits_magic{'H', 'I', 'T', 'S'};
    const auto hits_probe = probe("renamed.bin", hits_magic.data(), hits_magic.size());
    assert(hits_probe.format == Format::Hits && hits_probe.content_confirmed);

    // v34: PAC (read-only archive browser/assembler) and MOT (motion reader)
    // are promoted; both are byte-identified, never by extension alone.
    const auto* pac = NativeModuleRegistry::find("PAC");
    const auto* mot = NativeModuleRegistry::find("MOT");
    assert(pac != nullptr && pac->format == Format::Pac && !pac->renderable);
    assert(mot != nullptr && mot->format == Format::Mot && !mot->renderable);
    // A lone MOT shows its curves as an image preview.
    assert(has_capability(mot->capabilities, ResourceCapability::ImagePreview));
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
    assert(tsc != nullptr && tsc->format == Format::Tsc && !tsc->renderable &&
           has_capability(tsc->capabilities, ResourceCapability::ImagePreview));
    assert(clt != nullptr && clt->format == Format::Clt && !clt->renderable &&
           has_capability(clt->capabilities, ResourceCapability::ImagePreview));
    const auto* script = NativeModuleRegistry::find("MotionScript");
    assert(script != nullptr && script->format == Format::MotionScript && !script->renderable &&
           has_capability(script->capabilities, ResourceCapability::ImagePreview));
    constexpr std::string_view tsc_text = "\r\n.TSC\t\n\t# RELATIVE\n<Finish>\n$";
    constexpr std::string_view clt_text = ";a.clt\nClothNum 1\nClothNo 0\nBone 2 Y\nEnd\n$";
    assert(probe("renamed.bin", reinterpret_cast<const std::uint8_t*>(tsc_text.data()),
                 tsc_text.size()).format == Format::Tsc);
    assert(probe("renamed.bin", reinterpret_cast<const std::uint8_t*>(clt_text.data()),
                 clt_text.size()).format == Format::Clt);

    const auto* colshape = NativeModuleRegistry::find("COLSHAPE");
    const auto* colindex = NativeModuleRegistry::find("COLINDEX");
    assert(colshape != nullptr && colshape->format == Format::CollisionShapes && !colshape->renderable);
    assert(colindex != nullptr && colindex->format == Format::AttackIndex && !colindex->renderable);
    assert(probe("slot_0006.colidx", nullptr, 0U).format == Format::AttackIndex);
    assert(!probe("slot_0006.colidx", nullptr, 0U).content_confirmed);

    const auto* fxbank = NativeModuleRegistry::find("FXBANK");
    assert(fxbank != nullptr && fxbank->format == Format::EffectBank && !fxbank->renderable);

    // FON bitmap fonts: an inspection and an image (the glyph sheet), no scene.
    const auto* fon = NativeModuleRegistry::find("FON");
    assert(fon != nullptr && fon->format == Format::Fon && !fon->renderable);
    assert(has_capability(fon->capabilities, ResourceCapability::ImagePreview));

    // EFM effect models: own family, MOD document route (0x1402F7A90).
    const auto* efm = NativeModuleRegistry::find("EFM");
    assert(efm != nullptr && efm->format == Format::Mod && efm->renderable);
    const std::array<std::uint8_t, 4> efm_magic{'E', 'F', 'M', ' '};
    const auto efm_probe = probe("renamed.bin", efm_magic.data(), efm_magic.size());
    assert(efm_probe.format == Format::Mod && std::string_view{efm_probe.family} == "EFM");

    // Stage text, name lists, DCA and LIG2 came back with readers that check
    // their bytes (module_families.cpp); the old LIG alias, NBZ and MRP did not.
    for (const std::string_view family : {"TXT", ".index", "DCA", "LIG2"}) {
        assert(NativeModuleRegistry::find(family) != nullptr);
    }
    for (const std::string_view family : {"LIG", "NBZ", "MRP"}) {
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
    assert(probe("renamed.bin", hits_magic.data(), hits_magic.size()).format == Format::Hits);
    assert(probe("texture.ptx", nullptr, 0U).format == Format::Ptx);
    assert(probe("legacy.tm2", nullptr, 0U).format == Format::Dds);

    assert(!probe("stage.hits", nullptr, 0U).recognized);
    assert(!probe("stage.dca", nullptr, 0U).recognized);
    assert(!probe("stage.pac", nullptr, 0U).recognized);
    assert(!probe("stage.txt", nullptr, 0U).recognized);
    assert(!probe("model.shw", nullptr, 0U).recognized);

    // The start Info lists every registered family, grouped, from the registry.
    const auto welcome = spider::actions::welcome_text("9.9.9", "abc123");
    assert(welcome.rfind("DMC Native Reader 9.9.9\nNative build: abc123", 0) == 0U);
    for (const auto& module : modules) assert(welcome.find(module.family) != std::string::npos);
    assert(welcome.find("Stage: POS, EVE, CAM") != std::string::npos);
    assert(welcome.find("Unpromoted") == std::string::npos);

    return 0;
}
