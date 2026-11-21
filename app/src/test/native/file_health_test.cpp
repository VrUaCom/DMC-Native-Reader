// File health (file_health.h): what the game would not take (empty, zero,
// foreign contents, a cut PAC, the Reader's own log) and the smaller
// mismatches (a copy's name, no reader), and how a session carries it.
#include <cassert>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "dmcresource/file_health.h"
#include "dmcresource/resource_session.h"

using namespace dmcresource;
using file_health::Level;

namespace {

std::vector<std::uint8_t> pac(std::uint32_t count, std::vector<std::uint32_t> offsets, std::size_t size) {
    std::vector<std::uint8_t> b(size, 0xAB);
    std::memcpy(b.data(), "PAC\0", 4U);
    std::memcpy(b.data() + 4U, &count, 4U);
    for (std::size_t i = 0; i < offsets.size(); ++i) std::memcpy(b.data() + 8U + i * 4U, &offsets[i], 4U);
    return b;
}

bool has(const file_health::Report& r, std::string_view title) {
    for (const auto& f : r.findings) {
        if (f.title.find(title) != std::string::npos) return true;
    }
    return false;
}

}  // namespace

int main() {
    // A sound PAC: fine.
    const auto good = pac(2U, {0x10U, 0x20U}, 0x40U);
    assert(file_health::check("st001cfg.pac", good, true).worst() == Level::fine);
    // Empty, all zero: problems.
    assert(file_health::check("st001cfg.pac", {}, false).worst() == Level::problem);
    const std::vector<std::uint8_t> zeros(512, 0U);
    assert(has(file_health::check("st001cfg.pac", zeros, false), "zero"));
    // The Reader's diagnostics log written over a configuration.
    const std::string log = "[2026-10-21 23:02:17] build=edd6 route=external name=em000.pac\n";
    const std::vector<std::uint8_t> logged(log.begin(), log.end());
    const auto over = file_health::check("st001cfg.pac", logged, false);
    assert(over.worst() == Level::problem && has(over, "diagnostics log") && over.findings.size() == 1U);
    // A ZIP under a game name; the same bytes under a .zip name are no business of ours.
    const std::vector<std::uint8_t> zip{'P', 'K', 3, 4, 1, 2, 3, 4};
    assert(has(file_health::check("pl000.pac", zip, false), "ZIP"));
    assert(!has(file_health::check("mods.zip", zip, false), "ZIP"));
    // A PAC whose slots run past its end: cut short.
    const auto cut = file_health::check("st002.pac", pac(3U, {0x14U, 0x80U, 0x400U}, 0x100U), true);
    assert(cut.worst() == Level::problem && has(cut, "cut short"));
    assert(has(file_health::check("st002.pac", pac(1000U, {}, 0x40U), true), "cut short"));
    // A copy's name: a notice only.
    const auto copy = file_health::check("st001cfg (1).pac", good, true);
    assert(copy.worst() == Level::notice && has(copy, "name"));
    assert(has(file_health::check("st001.pac.pac", good, true), "doubled"));
    // Not recognized: a notice; notes become notices.
    assert(file_health::check("thing.bin", std::vector<std::uint8_t>(16, 7U), false).worst() == Level::notice);
    const std::vector<std::string> notes{"descriptors by a community tool"};
    assert(file_health::check("a.ptx", std::vector<std::uint8_t>(16, 7U), true, notes).worst() == Level::notice);
    // The report text.
    const auto text = file_health::format("st001cfg.pac", over);
    assert(text.rfind("FILE HEALTH", 0) == 0U && text.find("would not load") != std::string::npos);
    assert(file_health::format("ok.pac", {}).find("No problems") != std::string::npos);
    // A session carries the check of the bytes it opened from.
    auto session = open_session("st001cfg.pac", logged.data(), logged.size());
    assert(session && session->health.worst() == Level::problem);
    return 0;
}
