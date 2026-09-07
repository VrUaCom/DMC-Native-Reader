#include <array>
#include <cassert>
#include <cstdint>
#include <string_view>

#include "dmcresource/dmc_resource.h"
#include "dmcresource/native_module.h"

int main() {
    using namespace dmcresource;

    const auto& modules = NativeModuleRegistry::modules();
    assert(modules.size() == 4U);

    const auto* scm = NativeModuleRegistry::find("SCM");
    const auto* mod = NativeModuleRegistry::find("MOD");
    const auto* dds = NativeModuleRegistry::find("DDS");
    const auto* ptx = NativeModuleRegistry::find("PTX");
    assert(scm != nullptr && scm->format == Format::Scm && scm->renderable);
    assert(mod != nullptr && mod->format == Format::Mod && mod->renderable);
    assert(dds != nullptr && dds->format == Format::Dds && !dds->renderable);
    assert(ptx != nullptr && ptx->format == Format::Ptx && !ptx->renderable);

    // Removed/archived families must not leak back into the clean registry.
    for (const std::string_view family : {
             "HITS", "TXT", ".index", "DCA", "LIG", "LIG2",
             "PAC", "PNST", "NBZ", "EFM", "MRP", "SHW"}) {
        assert(NativeModuleRegistry::find(family) == nullptr);
    }

    const std::array<std::uint8_t, 4> scm_magic{'S', 'C', 'M', ' '};
    const std::array<std::uint8_t, 4> mod_magic{'M', 'O', 'D', ' '};
    const std::array<std::uint8_t, 4> dds_magic{'D', 'D', 'S', ' '};
    assert(probe("renamed.bin", scm_magic.data(), scm_magic.size()).format == Format::Scm);
    assert(probe("renamed.bin", mod_magic.data(), mod_magic.size()).format == Format::Mod);
    assert(probe("renamed.bin", dds_magic.data(), dds_magic.size()).format == Format::Dds);
    assert(probe("texture.ptx", nullptr, 0U).format == Format::Ptx);

    assert(!probe("stage.hits", nullptr, 0U).recognized);
    assert(!probe("stage.dca", nullptr, 0U).recognized);
    assert(!probe("stage.pac", nullptr, 0U).recognized);
    assert(!probe("stage.pnst", nullptr, 0U).recognized);
    assert(!probe("stage.txt", nullptr, 0U).recognized);
    assert(!probe("model.shw", nullptr, 0U).recognized);

    return 0;
}
