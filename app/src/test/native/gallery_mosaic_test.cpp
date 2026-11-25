// Gallery tile of a PAC slot holding a PTX: the textures themselves, not the
// evidence card. 1 texture fills the tile, 2..4 sit in a 2x2 grid, 5+ show
// three textures and "+N". Synthetic data, no game files.
#include "dmcresource/resource_session.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {

void put32(std::vector<std::uint8_t>& b, std::size_t at, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) b[at + i] = static_cast<std::uint8_t>((v >> (8 * i)) & 0xFFU);
}
void put16(std::vector<std::uint8_t>& b, std::size_t at, std::uint32_t v) {
    b[at] = static_cast<std::uint8_t>(v & 0xFFU);
    b[at + 1] = static_cast<std::uint8_t>((v >> 8) & 0xFFU);
}

// Interface-pack PTX (id*.pac layout): `count` 64x64 single-level DXT5
// textures, all opaque `rgb565`.
std::vector<std::uint8_t> make_ptx(std::uint32_t count, std::uint16_t rgb565) {
    constexpr std::uint32_t w = 64, h = 64, payload = (w / 4) * (h / 4) * 16;
    constexpr std::uint32_t span = (0x70 + 128 + payload + 0x7FF) / 0x800;
    std::vector<std::uint8_t> b(0x800 + count * span * 0x800);
    put32(b, 0, count);
    for (std::uint32_t t = 0; t < count; ++t) {
        put32(b, 4 + 4 * t, span);
        const std::size_t at = 0x800 + t * span * 0x800;
        put32(b, at + 0x08, 0x000201A5U);
        put32(b, at + 0x0C, 0x0000AAE4U);
        put16(b, at + 0x10, w);
        put16(b, at + 0x12, h);
        put32(b, at + 0x14, 1U);
        put32(b, at + 0x18, (w / 4) * 16);
        put32(b, at + 0x20, 0x40U);
        put32(b, at + 0x38, payload);
        put16(b, at + 0x44, w);
        put16(b, at + 0x46, h);
        float rw = 1.0F / w;
        std::memcpy(&b[at + 0x48], &rw, 4);
        std::memcpy(&b[at + 0x4C], &rw, 4);
        put32(b, at + 0x60, 5U);
        put32(b, at + 0x64, 128U + payload);
        put32(b, at + 0x68, 8U);
        const std::size_t d = at + 0x70;
        std::memcpy(&b[d], "DDS ", 4);
        put32(b, d + 4, 124U);
        put32(b, d + 8, 0x81007U);
        put32(b, d + 12, h);
        put32(b, d + 16, w);
        put32(b, d + 20, payload);
        put32(b, d + 76, 32U);
        put32(b, d + 80, 4U);
        std::memcpy(&b[d + 84], "DXT5", 4);
        put32(b, d + 108, 0x1000U);
        for (std::size_t p = d + 128; p < d + 128 + payload; p += 16) {
            b[p] = 0xFF;
            b[p + 1] = 0xFF;
            put16(b, p + 8, rgb565);
            put16(b, p + 10, rgb565);
        }
    }
    return b;
}

std::vector<std::uint8_t> make_pac(const std::vector<std::vector<std::uint8_t>>& slots) {
    const std::size_t header = 8 + 4 * slots.size();
    std::size_t at = (header + 15) & ~std::size_t{15};
    std::vector<std::uint8_t> b(at);
    std::memcpy(b.data(), "PAC\0", 4);
    put32(b, 4, static_cast<std::uint32_t>(slots.size()));
    for (std::size_t i = 0; i < slots.size(); ++i) {
        put32(b, 8 + 4 * i, static_cast<std::uint32_t>(b.size()));
        b.insert(b.end(), slots[i].begin(), slots[i].end());
    }
    return b;
}

const std::uint8_t* px(const dmcresource::ImagePreview& im, std::uint32_t x, std::uint32_t y) {
    return &im.rgba8[(static_cast<std::size_t>(y) * im.width + x) * 4];
}
bool red(const std::uint8_t* p) { return p[0] == 255 && p[1] == 0 && p[2] == 0 && p[3] == 255; }

}  // namespace

int main() {
    const auto pac = make_pac({make_ptx(1, 0xF800), make_ptx(2, 0xF800), make_ptx(6, 0xF800)});
    auto session = dmcresource::open_session("mosaic.pac", pac.data(), pac.size());
    assert(session && dmcresource::session_child_count(session.get()) == 3U);

    dmcresource::ImagePreview one, two, six;
    const auto* t1 = dmcresource::session_child_preview(session.get(), 0, &one);
    const auto* t2 = dmcresource::session_child_preview(session.get(), 1, &two);
    const auto* t6 = dmcresource::session_child_preview(session.get(), 2, &six);
    assert(t1 && t2 && t6);
    assert(t1->width == 256U && t2->width == 256U && t6->width == 256U);

    // One texture: the whole tile.
    assert(red(px(*t1, 5, 5)) && red(px(*t1, 250, 250)));
    // Two textures: top row filled, bottom row empty cells.
    assert(red(px(*t2, 60, 60)) && red(px(*t2, 190, 60)));
    assert(!red(px(*t2, 60, 190)) && !red(px(*t2, 190, 190)));
    // Six textures: three images and "+3" in the fourth cell.
    assert(red(px(*t6, 60, 60)) && red(px(*t6, 190, 60)) && red(px(*t6, 60, 190)));
    std::size_t label = 0;
    for (std::uint32_t y = 130; y < 256; ++y) {
        for (std::uint32_t x = 130; x < 256; ++x) {
            const auto* p = px(*t6, x, y);
            if (p[0] == 200 && p[1] == 202 && p[2] == 214) ++label;
        }
    }
    assert(label > 50);
    std::puts("gallery_mosaic_test: ok");
    return 0;
}
