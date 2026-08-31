#include <cassert>
#include <cstdint>
#include <cstring>
#include <string_view>

#include "dmcresource/exe_format_registry.h"

namespace {

dmcresource::ProbeResult probe(const char* text, std::size_t size) {
    return dmcresource::probe_exe_runtime_identity(
            reinterpret_cast<const std::uint8_t*>(text), size);
}

void expect_family(const char* bytes, std::size_t size, std::string_view family) {
    const auto result = probe(bytes, size);
    assert(result.recognized);
    assert(result.content_confirmed);
    assert(result.format == dmcresource::Format::Other);
    assert(std::string_view(result.family) == family);
}

void expect_none(const char* bytes, std::size_t size) {
    const auto result = probe(bytes, size);
    assert(!result.recognized);
}

}  // namespace

int main() {
    // 0x1402DB1F0: only bytes 0..2 are significant on this scoped path.
    expect_family("MOD ", 4, "MOD");
    expect_family("MODX", 4, "MOD");
    expect_family("MOD", 3, "MOD");
    expect_family("EFMZ", 4, "EFM");
    expect_family("SCMX", 4, "SCM");
    expect_family("MRP?", 4, "MRP");
    expect_family("SHW0", 4, "SHW");

    // Container dispatcher sentinels.
    expect_family("EFE!", 4, "EFE");
    expect_family("EFW!", 4, "EFW");
    expect_family("PNST", 4, "PNST");

    // 0x1402FD650: MCV is a four-byte identity and requires trailing space.
    expect_family("MCV ", 4, "MCV");
    expect_none("MCVX", 4);
    expect_none("MCV", 3);

    // Other direct canonical EXE content checks.
    expect_family("VAGp", 4, "VAGp");
    const char tm2[] = {'T', 'M', '2', '\0'};
    expect_family(tm2, 4, "TIM2");
    expect_family("DDS ", 4, "DDS");
    expect_none("TM2X", 4);
    expect_none("VAGx", 4);

    // Do not leak data/corpus HITS identity into the EXE runtime registry.
    expect_none("HITS", 4);
    expect_none("HITS$", 5);
    expect_none("PNSX", 4);

    // Reference/object-tag evidence is deliberately separate from content magic.
    assert(dmcresource::exe_format_evidence("TIM2").present);
    assert(dmcresource::exe_format_evidence("VAGp").present);
    assert(dmcresource::exe_format_evidence("DDS").present);
    assert(dmcresource::exe_format_evidence("LIG2").present);
    assert(dmcresource::exe_format_evidence("AFS namespace").present);
    assert(dmcresource::exe_format_evidence("EventTbl").present);
    assert(!dmcresource::exe_format_evidence("HITS").present);

    return 0;
}
