// FON bitmap fonts in Native Reader: a synthetic font built here in the
// layout (pages by UTF-16 high byte, glyph numbers, 1-bit glyphs) is probed
// as FON, read, inspected and drawn as a glyph sheet.

#include "dmcresource/decode_pipeline.h"
#include "dmcresource/dmc_resource.h"
#include "dmcresource/font.h"
#include "dmcresource/native_module.h"

#include <cassert>
#include <cstring>
#include <iostream>
#include <vector>

namespace {

// Two pages (U+00xx, U+25xx), three glyphs of 24 x 20.
std::vector<std::uint8_t> synthetic_font() {
    std::vector<std::uint8_t> bytes(0x100U + 2U * 0x200U + 3U * 60U, 0U);
    bytes[0x00] = 1U;
    bytes[0x25] = 2U;
    const auto map = [&](std::size_t page, std::uint8_t low, std::uint16_t glyph) {
        std::memcpy(bytes.data() + 0x100U + (page - 1U) * 0x200U + low * 2U, &glyph, 2U);
    };
    map(1U, 'A', 1U);
    map(1U, 'B', 2U);
    map(2U, 0xA0U, 3U);
    for (int row = 3; row <= 16; ++row) bytes[0x500U + row * 3] = 0x20U; // 'A': column 2
    for (int row = 4; row <= 15; ++row) {
        bytes[0x500U + 120U + row * 3] = 0xFFU;
        bytes[0x500U + 120U + row * 3 + 1] = 0xFFU;
    }
    return bytes;
}

}  // namespace

int main() {
    using namespace dmcresource;
    const auto bytes = synthetic_font();
    const auto read = font::read(bytes);
    assert(read.ok());
    assert(read.font->glyph_count == 3U && read.font->width == 24U && read.font->height == 20U);
    assert(read.font->characters[2].code == 0x25A0U);
    const auto ink = font::ink_span(*read.font, bytes, 3U);
    assert(ink && ink->first == 0U && ink->last == 15U);

    auto broken = bytes;
    broken.push_back(0U);
    assert(!font::read(broken).ok());

    const auto probed = probe("euro_test.bin", bytes.data(), bytes.size());
    assert(probed.format == Format::Fon && probed.content_confirmed);
    assert(std::string_view{format_name(Format::Fon)} == "FON");
    const auto* module = NativeModuleRegistry::find("FON");
    assert(module != nullptr && module->format == Format::Fon);

    const auto result = run_decode_pipeline("test.fon", bytes.data(), bytes.size());
    assert(result.accepted && result.inspection.format == "FON");
    assert(result.image_preview.available());
    assert(result.image_preview.width == 3U * 25U + 1U && result.image_preview.height == 22U);
    std::cout << "font_test: FON probed, read, inspected and drawn\n";
    return 0;
}
