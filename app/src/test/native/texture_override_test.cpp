// Pictures of your own over the model's textures (texture_override.h): the
// PNG decoder on hand-made files, and override / restore on a session.
#include <array>
#include <cassert>
#include <cstdint>
#include <string>
#include <vector>

#include "dmcresource/resource_session.h"
#include "dmcresource/texture_override.h"

using namespace dmcresource;

namespace {
void be32(std::vector<std::uint8_t>& b, std::uint32_t v) {
    for (int s = 24; s >= 0; s -= 8) b.push_back(static_cast<std::uint8_t>(v >> s));
}
// A zlib stream of stored blocks (no compression).
std::vector<std::uint8_t> zlib_stored(const std::vector<std::uint8_t>& data) {
    std::vector<std::uint8_t> z{0x78, 0x01};
    std::size_t o = 0;
    do {
        const std::size_t n = std::min<std::size_t>(65535U, data.size() - o);
        const bool last = o + n == data.size();
        z.push_back(last ? 1U : 0U);
        z.push_back(static_cast<std::uint8_t>(n));
        z.push_back(static_cast<std::uint8_t>(n >> 8));
        z.push_back(static_cast<std::uint8_t>(~n));
        z.push_back(static_cast<std::uint8_t>(~n >> 8));
        z.insert(z.end(), data.begin() + static_cast<long>(o), data.begin() + static_cast<long>(o + n));
        o += n;
    } while (o < data.size());
    std::uint32_t a = 1, b = 0;
    for (auto c : data) { a = (a + c) % 65521U; b = (b + a) % 65521U; }
    be32(z, (b << 16) | a);
    return z;
}
void chunk(std::vector<std::uint8_t>& png, const char* type, const std::vector<std::uint8_t>& data) {
    be32(png, static_cast<std::uint32_t>(data.size()));
    png.insert(png.end(), type, type + 4);
    png.insert(png.end(), data.begin(), data.end());
    be32(png, 0U);  // CRC (not checked)
}
std::vector<std::uint8_t> make_png(std::uint32_t w, std::uint32_t h, int depth, int colour,
                                   const std::vector<std::uint8_t>& rows, const std::vector<std::uint8_t>& plte = {},
                                   const std::vector<std::uint8_t>& trns = {}) {
    std::vector<std::uint8_t> png{137, 80, 78, 71, 13, 10, 26, 10};
    std::vector<std::uint8_t> ihdr;
    be32(ihdr, w);
    be32(ihdr, h);
    ihdr.insert(ihdr.end(), {static_cast<std::uint8_t>(depth), static_cast<std::uint8_t>(colour), 0, 0, 0});
    chunk(png, "IHDR", ihdr);
    if (!plte.empty()) chunk(png, "PLTE", plte);
    if (!trns.empty()) chunk(png, "tRNS", trns);
    chunk(png, "IDAT", zlib_stored(rows));
    chunk(png, "IEND", {});
    return png;
}
}  // namespace

int main() {
    std::string error;
    // RGBA 2x2: row 0 unfiltered, row 1 "Up" (adds the row above).
    const std::vector<std::uint8_t> rgba_rows{0, 10, 20, 30, 255, 40, 50, 60, 128,
                                              2, 1, 1, 1, 0, 2, 2, 2, 0};
    auto img = decode_png(make_png(2, 2, 8, 6, rgba_rows), &error);
    assert(img && img->width == 2U && img->height == 2U);
    assert(img->rgba8[0] == 10 && img->rgba8[3] == 255 && img->rgba8[7] == 128);
    assert(img->rgba8[8] == 11 && img->rgba8[11] == 255 && img->rgba8[12] == 42);

    // Palette, 2 bits per pixel, with transparency: 4 pixels in one byte.
    const std::vector<std::uint8_t> pal_rows{0, 0b00011011};
    img = decode_png(make_png(4, 1, 2, 3, pal_rows, {255, 0, 0, 0, 255, 0, 0, 0, 255, 9, 9, 9}, {0, 255}), &error);
    assert(img && img->rgba8[0] == 255 && img->rgba8[3] == 0);    // index 0 transparent red
    assert(img->rgba8[5] == 255 && img->rgba8[7] == 255);         // index 1 green
    assert(img->rgba8[12] == 9);                                  // index 3

    // 16-bit grey takes the high byte; grey 1-bit scales to 255.
    img = decode_png(make_png(1, 1, 16, 0, {0, 0xAB, 0xCD}), &error);
    assert(img && img->rgba8[0] == 0xAB && img->rgba8[3] == 255);
    img = decode_png(make_png(8, 1, 1, 0, {0, 0b10000000}), &error);
    assert(img && img->rgba8[0] == 255 && img->rgba8[4] == 0);

    // Refused: not a PNG, interlaced, truncated data, absurd size.
    assert(!decode_png(std::vector<std::uint8_t>{1, 2, 3}, &error));
    auto interlaced = make_png(1, 1, 8, 6, {0, 1, 2, 3, 4});
    interlaced[8 + 8 + 12] = 1;  // IHDR interlace byte
    assert(!decode_png(interlaced, &error) && error.find("interlac") != std::string::npos);
    assert(!decode_png(make_png(2, 2, 8, 6, {0, 1, 2}), &error));
    assert(!decode_png(make_png(9000, 1, 8, 6, {0}), &error));
    assert(!decode_texture_file(std::vector<std::uint8_t>{'D', 'D', 'S', ' ', 0}, &error));

    // Override and restore on a session's render bank.
    Session session;
    ImagePreview a;
    a.width = a.height = 2;
    a.rgba8.assign(16, 7);
    session.attached_textures = {a, a};
    ImagePreview mine;
    mine.width = 4;
    mine.height = 4;
    mine.rgba8.assign(64, 200);
    assert(!override_texture(&session, 5, mine, "x.png"));
    assert(override_texture(&session, 1, mine, "costume.png"));
    auto slots = texture_slots(&session);
    assert(slots.size() == 2U && slots[1].overridden && slots[1].width == 4U && slots[1].override_name == "costume.png");
    assert(texture_image(&session, 1, 0, true).rgba8[0] == 7);   // the original
    assert(texture_image(&session, 1, 2, false).width == 2U);    // scaled
    // A second picture keeps the first original.
    mine.rgba8.assign(64, 100);
    assert(override_texture(&session, 1, mine, "v2.png"));
    assert(restore_texture(&session, static_cast<std::size_t>(-1)));
    assert(session.attached_textures[1].rgba8[0] == 7 && session.texture_overrides.empty());
    assert(!restore_texture(&session, 1));
    return 0;
}
