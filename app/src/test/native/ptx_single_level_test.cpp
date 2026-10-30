// Retail single-level PTX (interface archives id*.pac): the usual bundle
// header and 0x70 descriptor per texture, followed by a DDS that holds only
// the base level (mip count 0). Synthetic bundle, no game data.
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

// Descriptor + DDS (DXT5, base level only) with blocks of one opaque colour.
void add_texture(std::vector<std::byte>& b, std::size_t at, std::uint32_t w, std::uint32_t h,
                 std::uint16_t rgb565) {
    const std::uint32_t payload = (w / 4U) * (h / 4U) * 16U;
    put32(b, at + 0x08, 0x000201A5U);
    put32(b, at + 0x0C, 0x0000AAE4U);
    put16(b, at + 0x10, w);
    put16(b, at + 0x12, h);
    put32(b, at + 0x14, 1U);
    put32(b, at + 0x18, (w / 4U) * 16U);
    put32(b, at + 0x20, 0x40U);
    put32(b, at + 0x38, payload);
    put16(b, at + 0x44, w);
    put16(b, at + 0x46, h);
    put32(b, at + 0x60, 5U);
    put32(b, at + 0x64, 128U + payload);
    put32(b, at + 0x68, 8U);
    const std::size_t d = at + 0x70;
    std::memcpy(&b[d], "DDS ", 4);
    put32(b, d + 4, 124U);
    put32(b, d + 8, 0x81007U);  // no DDSD_MIPMAPCOUNT
    put32(b, d + 12, h);
    put32(b, d + 16, w);
    put32(b, d + 20, payload);
    put32(b, d + 76, 32U);
    put32(b, d + 80, 4U);
    std::memcpy(&b[d + 84], "DXT5", 4);
    put32(b, d + 108, 0x1000U);
    std::size_t p = d + 128;
    for (std::uint32_t i = 0; i < payload / 16U; ++i, p += 16) {
        b[p] = std::byte{0xFF};
        b[p + 1] = std::byte{0xFF};
        put16(b, p + 8, rgb565);
        put16(b, p + 10, rgb565);
    }
}

}  // namespace

int main() {
    using namespace dmcresource::texture_set;
    // 64x64: 0x70 + 128 + 4096 bytes -> 3 sectors; 32x32: 0x70 + 128 + 1024 -> 1 sector.
    std::vector<std::byte> bank(0x800 + 3 * 0x800 + 1 * 0x800);
    put32(bank, 0, 2U);
    put32(bank, 4, 3U);
    put32(bank, 8, 1U);
    add_texture(bank, 0x800, 64, 64, 0xF800);   // red
    add_texture(bank, 0x2000, 32, 32, 0x001F);  // blue

    const auto set = parse_ptx(bank);
    assert(set.ok() && set.kind == Kind::ptx_bundle && set.ptx_single_level);
    assert(set.slots.size() == 2U);
    assert(set.slots[0].dds.width == 64U && set.slots[0].dds.mip_count == 1U);
    assert(set.slots[0].dds_offset == 0x870U && set.slots[1].dds_offset == 0x2070U);

    dmcresource::ImagePreview image;
    assert(decode_base_mip(bank, set.slots[0], &image));
    assert(image.width == 64U && image.rgba8[0] >= 0xF0 && image.rgba8[2] == 0 && image.rgba8[3] == 0xFF);
    assert(decode_base_mip(bank, set.slots[1], &image));
    assert(image.width == 32U && image.rgba8[0] == 0 && image.rgba8[2] >= 0xF0);

    // Strictness: descriptor size disagreeing with the DDS, a mip chain flag,
    // non-zero sector padding and trailing sectors all reject.
    auto bad = bank;
    put32(bad, 0x800 + 0x64, 1234U);
    assert(!parse_ptx(bad).ok());
    auto chain = bank;
    put32(chain, 0x870 + 8, 0x81007U | 0x20000U);
    assert(!parse_ptx(chain).ok());
    auto dirty = bank;
    dirty[0x800 + 0x70 + 128 + 4096 + 8] = std::byte{1};
    assert(!parse_ptx(dirty).ok());
    auto longer = bank;
    longer.resize(bank.size() + 0x800);
    assert(!parse_ptx(longer).ok());
    std::printf("ptx_single_level ok\n");
    return 0;
}
