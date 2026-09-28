#include "dmcresource/portable_session.h"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace {

void put_u16(std::vector<std::uint8_t>& bytes, std::size_t offset,
             std::uint16_t value) {
    assert(offset + 2U <= bytes.size());
    bytes[offset + 0U] = static_cast<std::uint8_t>(value & 0xFFU);
    bytes[offset + 1U] = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
}

void put_u32(std::vector<std::uint8_t>& bytes, std::size_t offset,
             std::uint32_t value) {
    assert(offset + 4U <= bytes.size());
    for (std::size_t i = 0U; i < 4U; ++i) {
        bytes[offset + i] = static_cast<std::uint8_t>(
            (value >> (i * 8U)) & 0xFFU);
    }
}

std::vector<std::uint8_t> make_dds() {
    constexpr std::size_t block_bytes = 8U;
    std::vector<std::uint8_t> bytes(128U + block_bytes * 3U, 0U);
    bytes[0] = 'D'; bytes[1] = 'D'; bytes[2] = 'S'; bytes[3] = ' ';
    put_u32(bytes, 4U, 124U);
    put_u32(bytes, 12U, 4U);
    put_u32(bytes, 16U, 4U);
    put_u32(bytes, 28U, 3U);
    put_u32(bytes, 76U, 32U);
    bytes[84] = 'D'; bytes[85] = 'X'; bytes[86] = 'T'; bytes[87] = '1';
    put_u16(bytes, 128U, 0xF800U);
    put_u16(bytes, 130U, 0x07E0U);
    put_u32(bytes, 132U, 0U);
    return bytes;
}

std::vector<std::uint8_t> make_ptx() {
    const auto dds = make_dds();
    std::vector<std::uint8_t> bytes(0x800U + 0x70U + dds.size(), 0U);
    put_u32(bytes, 0U, 1U);
    put_u32(bytes, 4U, 0U);
    put_u32(bytes, 0x800U + 0x38U,
            static_cast<std::uint32_t>(dds.size() - 128U));
    put_u32(bytes, 0x800U + 0x64U,
            static_cast<std::uint32_t>(dds.size()));
    std::memcpy(bytes.data() + 0x870U, dds.data(), dds.size());
    return bytes;
}

}  // namespace

int main() {
    std::string error;

    const auto dds = make_dds();
    auto dds_session = dmcresource::PortableSession::decode(
        "texture.dds", dds.data(), dds.size(), &error);
    assert(dds_session != nullptr);
    assert(!dds_session->has_geometry());
    assert(!dds_session->hierarchy_available());
    assert(dds_session->has_image_preview());
    assert(dds_session->child_count() == 0U);
    assert(dds_session->result().image_preview.available());
    assert(dds_session->result().image_preview.width == 4U);
    assert(dds_session->result().image_preview.height == 4U);
    assert(dds_session->summary().find("DDS") != std::string::npos);
    assert(dds_session->inspection_text().find("DDS") != std::string::npos);

    const auto ptx = make_ptx();
    auto ptx_session = dmcresource::PortableSession::decode(
        "bundle.ptx", ptx.data(), ptx.size(), &error);
    assert(ptx_session != nullptr);
    assert(!ptx_session->has_geometry());
    assert(!ptx_session->has_image_preview());
    assert(ptx_session->child_count() == 1U);
    const auto* child = ptx_session->child(0U);
    assert(child != nullptr);
    assert(child->probe.format == dmcresource::Format::Dds);
    assert(child->image_preview.available());
    assert(child->title == "DDS 0");

    // Generic render flags must not manufacture output for non-geometry
    // resources or bypass capability/evidence gates.
    dmcresource::ViewState view;
    const auto flags = dmcresource::render_flag(dmcresource::RenderFlag::Wireframe) |
                       dmcresource::render_flag(dmcresource::RenderFlag::Hierarchy);
    assert(dds_session->render(256, 256, view, flags).pixels.empty());
    assert(ptx_session->render(256, 256, view, flags).pixels.empty());

    std::vector<std::uint8_t> unknown(64U, 0x5AU);
    error.clear();
    auto rejected = dmcresource::PortableSession::decode(
        "unknown.bin", unknown.data(), unknown.size(), &error);
    assert(rejected == nullptr);
    assert(!error.empty());

    return 0;
}
