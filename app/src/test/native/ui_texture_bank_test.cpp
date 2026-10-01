// UI texture bank of the interface archives (id*.pac): sector-span header,
// per-texture 0x800 header (+0x10 width, +0x12 height, +0x18 row pitch,
// +0x20 format 0x40) and raw DXT blocks. Synthetic bank, no game data.
#include "dmcresource/texture_set.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {

void put32(std::vector<std::byte>& b, std::size_t at, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) b[at + i] = static_cast<std::byte>((v >> (8 * i)) & 0xFFU);
}
void put16(std::vector<std::byte>& b, std::size_t at, std::uint32_t v) {
    b[at] = static_cast<std::byte>(v & 0xFFU);
    b[at + 1] = static_cast<std::byte>((v >> 8) & 0xFFU);
}

// One texture record: header + DXT5 blocks of a single opaque colour.
void add_texture(std::vector<std::byte>& b, std::size_t at, std::uint32_t w, std::uint32_t h,
                 std::uint16_t rgb565) {
    put32(b, at + 0x08, 0x000201A5U);
    put32(b, at + 0x0C, 0x0000AAE4U);
    put16(b, at + 0x10, w);
    put16(b, at + 0x12, h);
    put32(b, at + 0x14, 1U);
    put32(b, at + 0x18, (w / 4U) * 16U);
    put32(b, at + 0x20, 0x40U);
    std::size_t p = at + 0x800;
    for (std::uint32_t i = 0; i < (w / 4U) * (h / 4U); ++i, p += 16) {
        b[p] = std::byte{0xFF};  // alpha0 = 255, alpha indices 0
        b[p + 1] = std::byte{0xFF};
        put16(b, p + 8, rgb565);
        put16(b, p + 10, rgb565);
    }
}

}  // namespace

int main() {
    using namespace dmcresource::texture_set;
    // Two textures: 64x64 (4096 bytes -> 1 + 2 sectors) and 32x32 (1024
    // bytes -> 1 + 1 sectors).
    std::vector<std::byte> bank(0x800 + 3 * 0x800 + 2 * 0x800);
    put32(bank, 0, 2U);
    put32(bank, 4, 3U);
    put32(bank, 8, 2U);
    add_texture(bank, 0x800, 64, 64, 0xF800);   // red
    add_texture(bank, 0x2000, 32, 32, 0x001F);  // blue

    const auto set = parse_ui_texture_bank(bank);
    assert(set.ok() && set.kind == Kind::ui_texture_bank && set.slots.size() == 2U);
    assert(set.slots[0].dds.width == 64U && set.slots[0].dds.height == 64U);
    assert(set.slots[1].dds.width == 32U && set.slots[1].sector_span == 2U);
    assert(set.slots[0].ui_format == 0x40U);

    // parse_ptx falls back to the UI bank when the canonical framing rejects.
    const auto via_ptx = parse_ptx(bank);
    assert(via_ptx.ok() && via_ptx.kind == Kind::ui_texture_bank);

    dmcresource::ImagePreview image;
    assert(decode_base_mip(bank, set.slots[0], &image));
    assert(image.width == 64U && image.rgba8.size() == 64U * 64U * 4U);
    assert(image.rgba8[0] >= 0xF0 && image.rgba8[2] == 0 && image.rgba8[3] == 0xFF);
    assert(decode_base_mip(bank, set.slots[1], &image));
    assert(image.width == 32U && image.rgba8[0] == 0 && image.rgba8[2] >= 0xF0);

    // Rejections: bad pitch, trailing sector, non-zero header padding.
    auto bad = bank;
    put32(bad, 0x800 + 0x18, 100U);
    assert(!parse_ui_texture_bank(bad).ok());
    auto longer = bank;
    longer.resize(bank.size() + 0x800);
    assert(!parse_ui_texture_bank(longer).ok());
    auto dirty = bank;
    dirty[0x400] = std::byte{1};
    assert(!parse_ui_texture_bank(dirty).ok());
    std::printf("ui_texture_bank ok\n");
    return 0;
}
