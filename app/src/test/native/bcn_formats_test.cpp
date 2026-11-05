// BC1..BC7 and DX10 DDS: standalone files and PTX bundles carrying them (the
// route for HD costume textures). Synthetic data, no game files.
#include "dmcresource/dds_bcn.h"
#include "dmcresource/texture_set.h"

#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {

namespace bcn = dmcresource::dds_bcn;
namespace ts = dmcresource::texture_set;

void put32(std::vector<std::byte>& b, std::size_t at, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) b[at + i] = static_cast<std::byte>((v >> (8 * i)) & 0xFFU);
}
void put16(std::vector<std::byte>& b, std::size_t at, std::uint32_t v) {
    b[at] = static_cast<std::byte>(v & 0xFFU);
    b[at + 1] = static_cast<std::byte>((v >> 8) & 0xFFU);
}

// BC7 mode-0 block and its pixels as decoded by an independent decoder
// (Pillow 12.3); the Rengine dds_bcn tests hold one vector per mode.
constexpr std::array<std::uint8_t, 16> k_bc7_block{
    0x79, 0x5c, 0xd7, 0x87, 0x94, 0xb7, 0xa1, 0x64, 0xe4, 0xcb, 0xd2, 0x1f, 0x99, 0x26, 0x30, 0xc2};
constexpr std::array<std::uint8_t, 8> k_bc7_first_pixels{0x66, 0x60, 0x46, 0xff, 0xb4, 0xb9, 0x24, 0xff};

// DDS (legacy FourCC, or DX10 when dxgi != 0) with every block = `block`.
std::vector<std::byte> make_dds(std::uint32_t w, std::uint32_t h, std::uint32_t mips,
                                const char* fourcc, std::uint32_t dxgi,
                                const std::uint8_t* block, std::uint32_t block_size) {
    const std::size_t header = dxgi != 0U ? 148U : 128U;
    std::size_t payload = 0U;
    for (std::uint32_t l = 0, lw = w, lh = h; l < mips; ++l, lw = lw > 1 ? lw / 2 : 1, lh = lh > 1 ? lh / 2 : 1) {
        payload += ((lw + 3U) / 4U) * ((lh + 3U) / 4U) * block_size;
    }
    std::vector<std::byte> b(header + payload);
    std::memcpy(&b[0], "DDS ", 4);
    put32(b, 4, 124U);
    put32(b, 8, 0x1007U | (mips > 1U ? 0x20000U : 0U));
    put32(b, 12, h);
    put32(b, 16, w);
    put32(b, 28, mips > 1U ? mips : 0U);
    put32(b, 76, 32U);
    put32(b, 80, 4U);
    std::memcpy(&b[84], dxgi != 0U ? "DX10" : fourcc, 4);
    put32(b, 108, 0x1000U);
    if (dxgi != 0U) {
        put32(b, 128, dxgi);
        put32(b, 132, 3U);
        put32(b, 140, 1U);
    }
    for (std::size_t p = header; p < b.size(); p += block_size) std::memcpy(&b[p], block, block_size);
    return b;
}

// PTX: bundle header, then per texture a gfxTexture header as dmc3.exe
// expects it (+0x20 = 0x40, +0x68 = 8, +0x64 = DDS size) and the DDS.
std::vector<std::byte> make_ptx(const std::vector<std::vector<std::byte>>& dds,
                                const std::vector<std::array<std::uint32_t, 2>>& logical) {
    std::vector<std::uint32_t> spans;
    for (const auto& d : dds) spans.push_back(static_cast<std::uint32_t>((0x70U + d.size() + 0x7FFU) / 0x800U));
    std::size_t total = 0x800U;
    for (auto s : spans) total += s * 0x800U;
    std::vector<std::byte> b(total);
    put32(b, 0, static_cast<std::uint32_t>(dds.size()));
    std::size_t at = 0x800U;
    for (std::size_t i = 0; i < dds.size(); ++i) {
        put32(b, 4 + 4 * i, spans[i]);
        put16(b, at + 0x10, logical[i][0]);
        put16(b, at + 0x12, logical[i][1]);
        put32(b, at + 0x20, 0x40U);
        put32(b, at + 0x64, static_cast<std::uint32_t>(dds[i].size()));
        put32(b, at + 0x68, 8U);
        std::memcpy(&b[at + 0x70], dds[i].data(), dds[i].size());
        at += spans[i] * 0x800U;
    }
    return b;
}

std::span<const std::byte> view(const std::vector<std::byte>& b) { return {b.data(), b.size()}; }

void standalone_bc7() {
    const auto dds = make_dds(8, 8, 1, nullptr, 98U, k_bc7_block.data(), 16);
    const auto set = ts::parse_dds(view(dds));
    assert(set.ok() && set.kind == ts::Kind::standalone_dds);
    const auto& slot = set.slots[0];
    assert(slot.dds.format == bcn::Format::bc7 && slot.dds.dx10_header && slot.dds.header_size == 148U);
    dmcresource::ImagePreview img;
    assert(ts::decode_base_mip(view(dds), slot, &img) && img.width == 8U);
    for (std::size_t i = 0; i < k_bc7_first_pixels.size(); ++i) assert(img.rgba8[i] == k_bc7_first_pixels[i]);
    // Same block repeated: pixel (4, 0) equals pixel (0, 0).
    assert(std::memcmp(&img.rgba8[16], &img.rgba8[0], 4) == 0);
}

void every_legacy_and_dx10_format_opens() {
    const std::array<std::uint8_t, 16> block{0x40, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
    struct C { const char* fourcc; std::uint32_t dxgi; std::uint32_t size; bcn::Format f; };
    const C cases[] = {
        {"DXT1", 0, 8, bcn::Format::bc1},   {"DXT3", 0, 16, bcn::Format::bc2},
        {"DXT5", 0, 16, bcn::Format::bc3},  {"ATI1", 0, 8, bcn::Format::bc4_unorm},
        {"BC4S", 0, 8, bcn::Format::bc4_snorm}, {"ATI2", 0, 16, bcn::Format::bc5_unorm},
        {"BC5S", 0, 16, bcn::Format::bc5_snorm}, {nullptr, 95, 16, bcn::Format::bc6h_uf16},
        {nullptr, 96, 16, bcn::Format::bc6h_sf16}, {nullptr, 98, 16, bcn::Format::bc7},
        {nullptr, 99, 16, bcn::Format::bc7},
    };
    for (const auto& c : cases) {
        const auto dds = make_dds(16, 16, 5, c.fourcc, c.dxgi, block.data(), c.size);
        const auto set = ts::parse_dds(view(dds));
        assert(set.ok() && set.slots[0].dds.format == c.f && set.slots[0].dds.mip_count == 5U);
        dmcresource::ImagePreview img;
        assert(ts::decode_base_mip(view(dds), set.slots[0], &img) && img.available());
    }
}

void ptx_with_bc7_and_bc4() {
    const auto bc7 = make_dds(64, 64, 7, nullptr, 98U, k_bc7_block.data(), 16);
    const std::array<std::uint8_t, 8> bc4{0xFF, 0x00, 0, 0, 0, 0, 0, 0};  // index 0 -> 255
    const auto gray = make_dds(32, 32, 1, "ATI1", 0, bc4.data(), 8);
    // Logical size 32x32 for the 64x64 BC7: an HD texture keeping the
    // original UV space, as the retail .tm2 font does.
    auto ptx = make_ptx({bc7, gray}, {{{32, 32}}, {{32, 32}}});
    const auto set = ts::parse_ptx(view(ptx));
    assert(set.ok() && set.kind == ts::Kind::ptx_bundle && set.ptx_extended_formats);
    assert(set.slots.size() == 2U);
    assert(set.slots[0].dds.format == bcn::Format::bc7 && set.slots[0].dds.width == 64U);
    assert(set.slots[0].dds_offset == 0x870U);
    assert(set.slots[1].dds.format == bcn::Format::bc4_unorm);
    dmcresource::ImagePreview img;
    assert(ts::decode_base_mip(view(ptx), set.slots[1], &img));
    assert(img.rgba8[0] == 255U && img.rgba8[1] == 255U && img.rgba8[3] == 255U);

    // dmc3.exe checks: self-relative pointers, vtable slot, DDS size.
    auto bad = ptx;
    put32(bad, 0x800 + 0x20, 0x48U);
    assert(!ts::parse_ptx(view(bad)).ok());
    bad = ptx;
    put32(bad, 0x800 + 0x68, 0U);
    assert(!ts::parse_ptx(view(bad)).ok());
    bad = ptx;
    put32(bad, 0x800, 1U);
    assert(!ts::parse_ptx(view(bad)).ok());
    bad = ptx;
    put32(bad, 0x800 + 0x64, static_cast<std::uint32_t>(bc7.size() - 16U));
    assert(!ts::parse_ptx(view(bad)).ok());
    bad = ptx;
    put16(bad, 0x800 + 0x10, 0U);
    assert(!ts::parse_ptx(view(bad)).ok());
    bad = ptx;
    bad.resize(bad.size() + 0x800U);  // sector past the declared spans
    assert(!ts::parse_ptx(view(bad)).ok());
}

void hd_preview_budget() {
    // 4096x4096 BC1 without mips: 16M pixels, previewed box-filtered to 2048.
    const std::array<std::uint8_t, 8> red{0x00, 0xF8, 0x00, 0xF8, 0, 0, 0, 0};
    const auto dds = make_dds(4096, 4096, 1, "DXT1", 0, red.data(), 8);
    const auto set = ts::parse_dds(view(dds));
    assert(set.ok());
    dmcresource::ImagePreview img;
    std::string detail;
    assert(ts::decode_base_mip(view(dds), set.slots[0], &img, &detail));
    assert(img.width == 2048U && img.height == 2048U);
    assert(img.rgba8[0] == 255U && img.rgba8[1] == 0U && img.rgba8[3] == 255U);
    assert(detail.find("box-filtered 1/2") != std::string::npos);
}

}  // namespace

int main() {
    standalone_bc7();
    every_legacy_and_dx10_format_opens();
    ptx_with_bc7_and_bc4();
    hd_preview_budget();
    std::puts("bcn_formats_test: ok");
    return 0;
}
