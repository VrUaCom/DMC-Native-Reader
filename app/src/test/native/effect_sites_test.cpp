// The effect spawn-site catalog of dmc3.exe (effect_spawn_sites.h): every
// row is well formed, and the sites already traced by hand for Lady (CEm034,
// dmc3-shell-effect-runtime-exe-v73.md) are in it with their modes.
#include <cassert>
#include <string_view>

#include "dmcresource/effect_spawn_sites.h"

using namespace dmcresource::effect_sites;

namespace {

bool has(char kind, std::uint16_t id, std::string_view owner, SpawnCall call, int mode) {
    for (const auto& s : spawn_sites()) {
        if (s.kind == kind && s.id == id && owner == s.owner && s.call == call && s.mode == mode) return true;
    }
    return false;
}

}  // namespace

int main() {
    const auto sites = spawn_sites();
    assert(sites.size() > 900U);
    std::size_t resolved = 0U;
    for (const auto& s : sites) {
        assert(s.address >= 0x140001000ULL && s.address < 0x140400000ULL);
        assert(s.function <= s.address);
        assert(s.owner != nullptr && s.owner[0] == 'C');
        assert(s.kind == 'P' || s.kind == 'E' || s.kind == 'G' || s.kind == 'V' || s.kind == '?');
        if (s.kind != '?' && s.id != 0xFFFFU) ++resolved;
    }
    assert(resolved > 850U);
    // CEm034 spawns V423 beside Shl02 through 0x1402E7A90, prep mode 3.
    assert(has('V', 423U, "CEm034", SpawnCall::spawn, 3));
    // CEm034Shl04 explodes into V488 through 0x1402E7CA0, flags 0x10.
    assert(has('V', 488U, "CEm034Shl04", SpawnCall::spawn_raised, 0x10));
    const auto owners488 = owners('V', 488U);
    assert(!owners488.empty() && owners488.front() == "CEm034Shl04");
    assert(owners('V', 60000U).empty());
    return 0;
}
