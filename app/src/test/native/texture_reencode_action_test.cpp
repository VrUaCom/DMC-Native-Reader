// Spider texture re-encode action on synthetic data (no game files): a PAC
// with an interface-style PTX, re-encoded as a whole and through its PTX
// child (the whole PAC is rebuilt, other slots untouched).
#include "dmcresource/dds_bcn.h"
#include "dmcresource/resource_session.h"
#include "dmcresource/spider/black_widow.h"
#include "dmcresource/spider/session_actions.h"
#include "dmcresource/texture_reencode.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>

namespace {

namespace bw = dmcresource::spider::black_widow;
namespace bcn = dmcresource::dds_bcn;

void put32(std::vector<std::uint8_t>& b, std::size_t at, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) b[at + i] = static_cast<std::uint8_t>((v >> (8 * i)) & 0xFFU);
}
void put16(std::vector<std::uint8_t>& b, std::size_t at, std::uint32_t v) {
    b[at] = static_cast<std::uint8_t>(v & 0xFFU);
    b[at + 1] = static_cast<std::uint8_t>((v >> 8) & 0xFFU);
}

// Interface PTX (id*.pac layout), one 64x64 single-level DXT5 texture.
std::vector<std::uint8_t> make_ptx() {
    constexpr std::uint32_t w = 64, h = 64, payload = (w / 4) * (h / 4) * 16;
    constexpr std::uint32_t span = (0x70 + 128 + payload + 0x7FF) / 0x800;
    std::vector<std::uint8_t> b(0x800 + span * 0x800);
    put32(b, 0, 1);
    put32(b, 4, span);
    const std::size_t at = 0x800;
    put32(b, at + 0x08, 0x000201A5U);
    put32(b, at + 0x0C, 0x0000AAE4U);
    put16(b, at + 0x10, w);
    put16(b, at + 0x12, h);
    put32(b, at + 0x14, 1U);
    put32(b, at + 0x18, w * 4);
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
        put16(b, p + 8, 0x07E0);  // green
        put16(b, p + 10, 0x07E0);
    }
    return b;
}

std::vector<std::uint8_t> make_pac(const std::vector<std::vector<std::uint8_t>>& slots) {
    std::vector<std::uint8_t> b((8 + 4 * slots.size() + 15) / 16 * 16);
    std::memcpy(b.data(), "PAC\0", 4);
    put32(b, 4, static_cast<std::uint32_t>(slots.size()));
    for (std::size_t i = 0; i < slots.size(); ++i) {
        b.resize((b.size() + 15) / 16 * 16);
        put32(b, 8 + 4 * i, static_cast<std::uint32_t>(b.size()));
        b.insert(b.end(), slots[i].begin(), slots[i].end());
    }
    return b;
}

std::span<const std::byte> bytes_of(const std::vector<std::uint8_t>& b) {
    return {reinterpret_cast<const std::byte*>(b.data()), b.size()};
}

bcn::Format first_texture_format(const std::vector<std::uint8_t>& file) {
    const auto slots = dmcresource::texture_reencode::read_pac_slots(bytes_of(file));
    assert(slots && !slots->empty());
    const auto dds = bytes_of(file).subspan((*slots)[0].offset + 0x870);
    return bcn::parse(dds).document.format;
}

}  // namespace

std::shared_ptr<const std::vector<std::uint8_t>> root_bytes_keepalive;

int main() {
    std::vector<std::uint8_t> other(300, 0x5A);
    const auto pac = make_pac({make_ptx(), other});
    auto root = dmcresource::open_session("ui.pac", pac.data(), pac.size());
    assert(root && root->source_bytes && root->source_bytes->size() == pac.size());
    assert(bw::has_state(dmcresource::black_widow_state(root.get()), bw::StateFlag::CanReencodeTextures));
    assert(!bw::has_state(dmcresource::black_widow_state(root.get()), bw::StateFlag::CanSaveSource));

    // Whole PAC -> BC7.
    std::string detail;
    auto bc7 = dmcresource::spider::actions::reencode_textures(root.get(), "bc7", false, &detail);
    assert(bc7 && bc7->authored && bc7->source_bytes);
    assert(bw::has_state(dmcresource::black_widow_state(bc7.get()), bw::StateFlag::CanSaveSource));
    assert(first_texture_format(*bc7->source_bytes) == bcn::Format::bc7);
    assert(bc7->source_name == "ui.pac");
    assert(detail.find("BC7") != std::string::npos);
    // The result still opens as a PAC with a viewable PTX child.
    assert(dmcresource::session_child_count(bc7.get()) == 2U);

    // Through the PTX child: the whole PAC comes back, slot 1 untouched.
    auto child = dmcresource::open_session_child(root.get(), 0);
    assert(child && child->source_bytes && child->container_source && child->container_slot == 0);
    assert(bw::has_state(dmcresource::black_widow_state(child.get()), bw::StateFlag::ReencodeRebuildsContainer));
    assert(!bw::has_state(dmcresource::black_widow_state(root.get()), bw::StateFlag::ReencodeRebuildsContainer));
    // The link outlives the parent session.
    root_bytes_keepalive = root->source_bytes;
    auto dxt1 = dmcresource::spider::actions::reencode_textures(child.get(), "dxt1", false, &detail);
    assert(dxt1 && dxt1->source_bytes);
    const auto& out = *dxt1->source_bytes;
    assert(out.size() >= 4 && std::memcmp(out.data(), "PAC\0", 4) == 0);
    assert(first_texture_format(out) == bcn::Format::bc1);
    const auto slots = dmcresource::texture_reencode::read_pac_slots(bytes_of(out));
    assert(std::memcmp(out.data() + (*slots)[1].offset, other.data(), other.size()) == 0);

    // Unknown format and a resource without textures fail with a reason.
    assert(!dmcresource::spider::actions::reencode_textures(root.get(), "png", false, &detail));
    auto plain = dmcresource::open_session("other.bin", other.data(), other.size());
    assert(plain && !plain->source_bytes);
    assert(!dmcresource::spider::actions::reencode_textures(plain.get(), "bc7", false, &detail));
    // The message names the Spider step that stopped.
    assert(detail.rfind("select source failed", 0) == 0);

    assert(dmcresource::spider::actions::texture_format_choices().size() == 10U);
    std::puts("texture_reencode_action_test: ok");
    return 0;
}
