// The coat's motion states of a player archive (motion/coat_states.h):
// parsing the entry table, the script's channel-3 state over time, and the
// cloth parameters a state gives.
#include <array>
#include <bit>
#include <cassert>
#include <cstdint>
#include <vector>

#include "dmcresource/motion/coat_states.h"

using namespace dmcresource::motion;

namespace {
void put16(std::vector<std::uint8_t>& b, std::size_t o, std::uint16_t v) {
    b[o] = static_cast<std::uint8_t>(v);
    b[o + 1U] = static_cast<std::uint8_t>(v >> 8U);
}
void putf(std::vector<std::uint8_t>& b, std::size_t o, float f) {
    const auto v = std::bit_cast<std::uint32_t>(f);
    for (std::size_t k = 0; k < 4U; ++k) b[o + k] = static_cast<std::uint8_t>(v >> (8U * k));
}
}  // namespace

int main() {
    // Three entries (12 bytes), a shared block at 0x0C, records at 0x0C and 0x20.
    std::vector<std::uint8_t> b(0x800U, 0U);
    put16(b, 0U, 0x0CU); put16(b, 2U, 0x0CU);
    put16(b, 4U, 0x0CU); put16(b, 6U, 0x20U);
    put16(b, 8U, 0x0CU); put16(b, 10U, 0x0CU);
    const std::array<float, 5> r0{0.3F, -0.02F, 0.0F, 0.0F, 0.0F};
    const std::array<float, 5> r1{0.8F, -0.02F, 0.0F, 0.0F, 0.16F};
    for (std::size_t k = 0; k < 5U; ++k) {
        putf(b, 0x0CU + k * 4U, r0[k]);
        putf(b, 0x20U + k * 4U, r1[k]);
    }
    const auto table = CoatStates::parse(b);
    assert(table && table->count() == 3U);
    assert(table->record(1)->offset == 0x20U && table->record(1)->stiffness == 0.8F);
    assert(table->record(1)->wind[2] == 0.16F && table->record(1)->gravity_y == -0.02F);
    assert(table->record(3) == nullptr);

    // Not a table: an entry pointing outside, or a zero half.
    auto bad = b;
    put16(bad, 6U, 0x7FF0U);
    assert(!CoatStates::parse(bad));
    auto zero = b;
    put16(zero, 2U, 0U);
    assert(!CoatStates::parse(zero));

    // The script's channel 3: the last non-zero value passed; 0 before any.
    std::vector<ScriptSignalKey> keys(3);
    keys[0].after_frame = 5.0F;
    keys[0].channels = {0, 0, 0, 2, 0};
    keys[1].after_frame = 10.0F;
    keys[1].channels = {0, 0, 0, 0, 0};
    keys[2].after_frame = 20.0F;
    keys[2].channels = {0, 0, 0, 1, 0};
    assert(coat_state_at(keys, 0.0F) == 0);
    assert(coat_state_at(keys, 6.0F) == 2);
    assert(coat_state_at(keys, 12.0F) == 2);
    assert(coat_state_at(keys, 25.0F) == 1);

    // A state's parameters over the .clt's.
    ClothParams base;
    base.gravity = {0.0F, -0.05F, 0.0F};
    base.stiffness = 0.3F;
    base.bones.push_back({2U, 1U});
    const auto p = coat_params(base, *table->record(1));
    assert(p.stiffness == 0.8F && p.gravity[1] == -0.02F && p.wind[2] == 0.16F && p.bones.size() == 1U);
    assert(p.max_speed == base.max_speed && p.spring_force == base.spring_force);
    return 0;
}
