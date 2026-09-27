#include <cassert>
#include <cstdint>
#include <cstring>
#include <vector>

#include "dmcresource/effect_bank.h"
#include "dmcresource/format_views.h"

namespace {

void put_u16(std::vector<std::uint8_t>& b, std::size_t o, std::uint16_t v) {
    b[o + 0U] = static_cast<std::uint8_t>(v & 0xFFU);
    b[o + 1U] = static_cast<std::uint8_t>((v >> 8U) & 0xFFU);
}

void put_u32(std::vector<std::uint8_t>& b, std::size_t o, std::uint32_t v) {
    for (std::size_t i = 0U; i < 4U; ++i) {
        b[o + i] = static_cast<std::uint8_t>((v >> (8U * i)) & 0xFFU);
    }
}

void put_f32(std::vector<std::uint8_t>& b, std::size_t o, float v) {
    std::uint32_t bits{};
    std::memcpy(&bits, &v, sizeof(bits));
    put_u32(b, o, bits);
}

dmcresource::effect_bank::Record record(
        char kind, std::uint32_t id, std::vector<std::uint8_t>& bytes) {
    dmcresource::effect_bank::Record out;
    out.kind = kind;
    out.id = id;
    out.bytes = bytes;
    return out;
}

}  // namespace

int main() {
    using namespace dmcresource;
    using namespace dmcresource::effect_bank;

    std::vector<std::uint8_t> v_bytes(368U, 0U);
    put_u16(v_bytes, 0x00U, 1U);
    v_bytes[0x04U] = 1U;  // E
    put_u16(v_bytes, 0x06U, 745U);
    put_f32(v_bytes, 0x0CU, 1.0F);
    put_f32(v_bytes, 0x10U, 2.0F);
    put_f32(v_bytes, 0x14U, 3.0F);
    put_f32(v_bytes, 0x18U, 0.0F);
    put_f32(v_bytes, 0x1CU, 90.0F);
    put_f32(v_bytes, 0x20U, 180.0F);
    put_f32(v_bytes, 0x24U, 1.0F);
    put_f32(v_bytes, 0x28U, 1.0F);
    put_f32(v_bytes, 0x2CU, 1.0F);
    auto v_record = record('V', 10U, v_bytes);
    const auto v = v_runtime_view(v_record);
    assert(v && v->entries.size() == 1U);
    assert(v->entries[0].dispatch == 1U);
    assert(v->entries[0].id == 745U);
    assert(v->entries[0].translation[2] == 3.0F);
    assert(v->entries[0].rotation_degrees[1] == 90.0F);
    assert(v->entries[0].scale[0] == 1.0F);
    assert(views::render_effect_record_view(v_record).available());
    assert(views::render_effect_visual_view(v_record).available());

    std::vector<std::uint8_t> e_bytes(544U, 0U);
    e_bytes[0x01U] = 1U;
    put_u16(e_bytes, 0x04U, 5U);
    e_bytes[0x06U] = 1U;
    put_u16(e_bytes, 0x08U, 110U);
    put_u16(e_bytes, 0x0CU, 0U);
    put_u16(e_bytes, 0x0EU, 96U);
    put_u16(e_bytes, 0x10U, 16U);
    put_u16(e_bytes, 0x12U, 16U);
    auto e_record = record('E', 745U, e_bytes);
    const auto e = e_runtime_view(e_record);
    assert(e && e->texture_id == 5U);
    assert(e->uses_animation);
    assert(e->animation_id == 110U);
    assert(e->rectangle.y == 96U);
    assert(e->rectangle.w == 16U);
    assert(views::render_effect_record_view(e_record).available());
    assert(views::render_effect_visual_view(e_record).available());

    std::vector<std::uint8_t> g_bytes(96U, 0U);
    put_u32(g_bytes, 0x20U, 700U);
    g_bytes[0x30U] = 1U;
    put_f32(g_bytes, 0x38U, 1.5F);
    put_f32(g_bytes, 0x3CU, 1.0F);
    put_u16(g_bytes, 0x40U, 15U);
    put_f32(g_bytes, 0x50U, 1.0F);
    put_f32(g_bytes, 0x54U, 1.0F);
    auto g_record = record('G', 214U, g_bytes);
    const auto g = g_runtime_view(g_record);
    assert(g && g->value_20 == 700);
    assert(g->steps_40 == 15U);
    assert(g->value_38 == 1.5F);
    assert(views::render_effect_record_view(g_record).available());
    assert(views::render_effect_visual_view(g_record).available());

    std::vector<std::uint8_t> p_bytes(336U, 0U);
    put_u32(p_bytes, 0x00U, 2U);
    put_u32(p_bytes, 0x10U, 0x10U);  // base 0x10 -> root 0x20
    put_u32(p_bytes, 0x14U, 0xF0U);  // base 0x10 -> list 0x100
    put_u32(p_bytes, 0x18U, 0x110U); // base 0x10 -> target 0x120
    p_bytes[0x21U] = 3U;
    put_u16(p_bytes, 0xE0U, 1U);     // root + 0xC0
    auto p_record = record('P', 22U, p_bytes);
    const auto p = p_runtime_view(p_record);
    assert(p && p->version == 2U);
    assert(p->subtype == 3U);
    assert(p->root_offset == 0x20U);
    assert(p->list_offset == 0x100U);
    assert(p->target_offsets.size() == 1U);
    assert(p->target_offsets[0] == 0x120U);
    assert(views::render_effect_record_view(p_record).available());
    assert(views::render_effect_visual_view(p_record).available());

    // Registered-but-not-yet-typed kinds still receive a concrete byte-map
    // visual rather than an empty tile.
    std::vector<std::uint8_t> c_bytes(64U, 0x55U);
    auto c_record = record('C', 7U, c_bytes);
    assert(views::render_effect_record_view(c_record).available());

    return 0;
}
