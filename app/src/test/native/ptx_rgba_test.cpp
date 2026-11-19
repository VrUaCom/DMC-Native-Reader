// PTX bundles with an uncompressed texture (GData.afs pl000 / em034 slot 0):
// the core encodes it as BC3 and the bundle reads like a retail one. Built
// from this test's own pixels, no game data.
#include "dmcresource/ptx_rgba.h"
#include "dmcresource/ptx_framing_compat.h"
#include "dmcresource/dds_bcn.h"

#include <cassert>
#include <cstdint>
#include <cstring>
#include <span>
#include <vector>

namespace {

void put(std::vector<std::uint8_t>& b, std::size_t at, std::uint32_t v) { std::memcpy(b.data() + at, &v, 4U); }
std::uint32_t get(const std::vector<std::uint8_t>& b, std::size_t at) {
    std::uint32_t v = 0;
    std::memcpy(&v, b.data() + at, 4U);
    return v;
}

// A DDS header: block (fourcc) or uncompressed 32-bit BGRA with alpha.
std::vector<std::uint8_t> dds_header(std::uint32_t w, std::uint32_t h, std::uint32_t fourcc) {
    std::vector<std::uint8_t> d(128U, 0U);
    std::memcpy(d.data(), "DDS ", 4U);
    put(d, 4U, 124U);
    put(d, 8U, 0x1007U);
    put(d, 12U, h);
    put(d, 16U, w);
    put(d, 28U, 1U);
    put(d, 76U, 32U);
    if (fourcc != 0U) {
        put(d, 80U, 0x4U);
        put(d, 84U, fourcc);
    } else {
        put(d, 80U, 0x41U);
        put(d, 88U, 32U);
        put(d, 92U, 0x00FF0000U);
        put(d, 96U, 0x0000FF00U);
        put(d, 100U, 0x000000FFU);
        put(d, 104U, 0xFF000000U);
    }
    put(d, 108U, 0x1000U);
    return d;
}

// Appends one 0x70-descriptor record of `dds` padded to sectors; returns its span.
std::uint32_t record(std::vector<std::uint8_t>& bundle, const std::vector<std::uint8_t>& dds) {
    const std::size_t start = bundle.size();
    bundle.resize(start + 0x70U, 0U);
    put(bundle, start + 0x64U, static_cast<std::uint32_t>(dds.size()));
    bundle.insert(bundle.end(), dds.begin(), dds.end());
    bundle.resize((bundle.size() + 0x7FFU) / 0x800U * 0x800U, 0U);
    return static_cast<std::uint32_t>((bundle.size() - start) / 0x800U);
}

}  // namespace

int main() {
    // Texture 0: a 4x4 DXT1 block (red). Texture 1: 8x8 uncompressed, green with half alpha.
    auto dxt1 = dds_header(4U, 4U, 0x31545844U);
    const std::uint8_t red_block[8] = {0x00, 0xF8, 0x00, 0xF8, 0, 0, 0, 0};
    dxt1.insert(dxt1.end(), std::begin(red_block), std::end(red_block));
    auto rgba = dds_header(8U, 8U, 0U);
    for (int i = 0; i < 64; ++i) {
        const std::uint8_t bgra[4] = {0x00, 0xFF, 0x00, 0x80};
        rgba.insert(rgba.end(), std::begin(bgra), std::end(bgra));
    }
    std::vector<std::uint8_t> bundle(0x800U, 0U);
    put(bundle, 0U, 2U);
    put(bundle, 4U, record(bundle, dxt1));
    put(bundle, 8U, record(bundle, rgba));
    const std::span<const std::byte> raw{reinterpret_cast<const std::byte*>(bundle.data()), bundle.size()};
    assert(!dmcresource::ptx_compat::parse_texture_bundle(raw).ok());  // rejected as it is

    const auto out = dmcresource::ptx_rgba::compress(bundle);
    assert(out && get(*out, 0U) == 2U && out->size() % 0x800U == 0U);
    // The DXT1 record is kept byte for byte at sector 1.
    assert(std::memcmp(out->data() + 0x800U + 0x70U, dxt1.data(), dxt1.size()) == 0);
    // The second record is now a BC3 DDS (DXT5) of 8x8, its size in the descriptor.
    const std::size_t second = (1U + get(*out, 4U)) * 0x800U;
    assert(std::memcmp(out->data() + second + 0x70U + 84U, "DXT5", 4U) == 0);
    const auto bc3_size = get(*out, second + 0x64U);
    assert(bc3_size == 128U + 4U * 16U);
    assert((1U + get(*out, 4U) + get(*out, 8U)) * 0x800U == out->size());

    // Its second texture decodes green with the alpha kept. (Whether the
    // bundle as a whole reads depends on the descriptor fields the game's
    // files carry; this test's descriptors are zero, so that is checked on
    // the real pl000 / em034 instead.)
    const std::span<const std::byte> fixed{reinterpret_cast<const std::byte*>(out->data()), out->size()};
    const auto dds = fixed.subspan(second + 0x70U, bc3_size);
    const auto doc = dmcresource::dds_bcn::parse(dds);
    assert(doc.ok() && doc.document.width == 8U && doc.document.format == dmcresource::dds_bcn::Format::bc3);
    const auto pixels = dmcresource::dds_bcn::decode_level_rgba8(dds, doc.document, 0U);
    assert(pixels.ok && pixels.image.rgba8.size() == 8U * 8U * 4U);
    const auto* p = pixels.image.rgba8.data();
    assert(p[0] < 8U && p[1] > 247U && p[2] < 8U && p[3] > 120U && p[3] < 136U);

    // Nothing uncompressed, or no bundle: left alone.
    std::vector<std::uint8_t> only_dxt(0x800U, 0U);
    put(only_dxt, 0U, 1U);
    put(only_dxt, 4U, record(only_dxt, dxt1));
    assert(!dmcresource::ptx_rgba::compress(only_dxt));
    assert(!dmcresource::ptx_rgba::compress(dxt1));
    return 0;
}
