// The effect spawn sites of dmc3.exe (effect_spawn_sites.h).
#include "dmcresource/effect_spawn_sites.h"

#include <algorithm>
#include <array>

namespace dmcresource::effect_sites {
namespace {

struct Row final {
    std::uint64_t address;
    std::uint64_t function;
    const char* owner;
    char kind;
    std::uint16_t id;
    std::uint8_t call;
    std::int16_t mode;
};

constexpr Row kRows[] = {
#include "effect_spawn_sites.inc"
};

constexpr auto kSites = [] {
    std::array<SpawnSite, std::size(kRows)> sites{};
    for (std::size_t i = 0U; i < std::size(kRows); ++i) {
        const auto& r = kRows[i];
        sites[i] = {r.address, r.function, r.owner, r.kind, r.id, static_cast<SpawnCall>(r.call), r.mode};
    }
    return sites;
}();

}  // namespace

std::span<const SpawnSite> spawn_sites() noexcept { return kSites; }

std::vector<std::string_view> owners(char kind, std::uint16_t id) {
    std::vector<std::string_view> out;
    for (const auto& site : kSites) {
        if (site.kind != kind || site.id != id) continue;
        const std::string_view owner = site.owner;
        if (std::find(out.begin(), out.end(), owner) == out.end()) out.push_back(owner);
    }
    return out;
}

}  // namespace dmcresource::effect_sites
