#include "dmcresource/motion/effect_runtime.h"

#include <algorithm>
#include <bit>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

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
    put_u32(b, o, std::bit_cast<std::uint32_t>(v));
}

std::vector<std::uint8_t> make_pnst(
    const std::vector<std::vector<std::uint8_t>>& payloads) {
    std::vector<std::uint8_t> out(8U + payloads.size() * 4U, 0U);
    out[0] = 'P'; out[1] = 'N'; out[2] = 'S'; out[3] = 'T';
    put_u32(out, 4U, static_cast<std::uint32_t>(payloads.size()));
    for (std::size_t i = 0U; i < payloads.size(); ++i) {
        if (payloads[i].empty()) continue;
        put_u32(out, 8U + i * 4U, static_cast<std::uint32_t>(out.size()));
        out.insert(out.end(), payloads[i].begin(), payloads[i].end());
    }
    return out;
}

struct Child {
    char kind{};
    std::uint16_t id{};
    std::array<float, 3> t{};
    std::array<float, 3> r{};
    std::array<float, 3> s{1.0F, 1.0F, 1.0F};
};

std::vector<std::uint8_t> make_v(std::initializer_list<Child> children) {
    std::vector<std::uint8_t> out(368U, 0U);
    put_u16(out, 0U, static_cast<std::uint16_t>(children.size()));
    std::size_t index = 0U;
    for (const auto& child : children) {
        const std::size_t base = index++ * 0x2CU;
        std::uint8_t dispatch = 0xFFU;
        if (child.kind == 'P') dispatch = 0U;
        if (child.kind == 'E') dispatch = 1U;
        if (child.kind == 'G') dispatch = 2U;
        if (child.kind == 'V') dispatch = 3U;
        assert(dispatch != 0xFFU);
        out[base + 0x04U] = dispatch;
        put_u16(out, base + 0x06U, child.id);
        for (std::size_t axis = 0U; axis < 3U; ++axis) {
            put_f32(out, base + 0x0CU + axis * 4U, child.t[axis]);
            put_f32(out, base + 0x18U + axis * 4U, child.r[axis]);
            put_f32(out, base + 0x24U + axis * 4U, child.s[axis]);
        }
    }
    return out;
}

std::vector<std::uint8_t> bytes(std::string_view s) {
    return {s.begin(), s.end()};
}

std::vector<std::uint8_t> make_bank(bool missing_child = false) {
    const auto manifest = bytes(
        missing_child
            ? "V 463\r\n# End\r\n"
            : "V 276\r\nE 571\r\nV 423\r\nE 752\r\nE 887\r\nP 337\r\n"
              "V 463\r\nE 741\r\nV 475\r\nE 404\r\nV 488\r\nP 18\r\n"
              "P 3\r\nP 16\r\nP 2\r\nE 1\r\nV 8\r\n# End\r\n");

    std::vector<std::vector<std::uint8_t>> records;
    if (missing_child) {
        records.push_back(make_v({{'E', 999U}}));
    } else {
        records.push_back(make_v({{'E', 571U}, {'E', 571U}}));
        records.push_back(std::vector<std::uint8_t>(544U, 1U));
        records.push_back(make_v({
            {'E', 752U, {60.0F, 0.0F, 0.0F}},
            {'E', 887U, {60.0F, 0.0F, 0.0F}},
            {'P', 337U, {60.0F, 0.0F, 0.0F}, {0.0F, 90.0F, 0.0F}},
        }));
        records.push_back(std::vector<std::uint8_t>(544U, 2U));
        records.push_back(std::vector<std::uint8_t>(544U, 3U));
        records.push_back(std::vector<std::uint8_t>(336U, 4U));
        records.push_back(make_v({{'E', 741U}}));
        records.push_back(std::vector<std::uint8_t>(544U, 5U));
        records.push_back(make_v({{'E', 404U}}));
        records.push_back(std::vector<std::uint8_t>(544U, 6U));
        records.push_back(make_v({
            {'P', 18U}, {'P', 3U}, {'P', 16U}, {'P', 2U},
            {'E', 1U, {0.0F, 15.0F, 0.0F}, {}, {1.1F, 1.1F, 1.1F}},
            {'V', 8U},
        }));
        records.push_back(std::vector<std::uint8_t>(528U, 7U));
        records.push_back(std::vector<std::uint8_t>(528U, 8U));
        records.push_back(std::vector<std::uint8_t>(336U, 9U));
        records.push_back(std::vector<std::uint8_t>(528U, 10U));
        records.push_back(std::vector<std::uint8_t>(544U, 11U));
        records.push_back(make_v({}));
    }
    return make_pnst({manifest, make_pnst(records)});
}

std::vector<std::uint32_t> active_ids(
    const dmcresource::motion::EffectRuntime& runtime) {
    std::vector<std::uint32_t> out;
    for (const auto& instance : runtime.active_instances()) {
        if (instance.active) out.push_back(instance.source.effect_id);
    }
    std::sort(out.begin(), out.end());
    return out;
}

}  // namespace

int main() {
    using namespace dmcresource;
    using namespace dmcresource::motion;

    EffectRuntime runtime;
    const auto bank = make_bank();
    assert(runtime.load_bank(bank, 28U));
    assert(runtime.loaded());
    assert(runtime.source_slot() == 28U);
    assert(runtime.catalog().size() == 17U);

    const auto* v276 = runtime.find('V', 276U);
    assert(v276 != nullptr && v276->source_slot == 0U);
    assert(v276->children.size() == 2U);
    assert((v276->children[0].key == EffectResourceKey{'E', 571U}));
    assert((v276->children[1].key == EffectResourceKey{'E', 571U}));

    const auto* v423 = runtime.find('V', 423U);
    assert(v423 != nullptr && v423->children.size() == 3U);
    assert((v423->children[0].key == EffectResourceKey{'E', 752U}));
    assert((v423->children[1].key == EffectResourceKey{'E', 887U}));
    assert((v423->children[2].key == EffectResourceKey{'P', 337U}));
    assert(v423->children[0].translation[0] == 60.0F);
    assert(v423->children[2].rotation_degrees[1] == 90.0F);

    const auto* v488 = runtime.find('V', 488U);
    assert(v488 != nullptr && v488->children.size() == 6U);
    assert((v488->children[0].key == EffectResourceKey{'P', 18U}));
    assert((v488->children[4].key == EffectResourceKey{'E', 1U}));
    assert((v488->children[5].key == EffectResourceKey{'V', 8U}));
    assert(v488->children[4].translation[1] == 15.0F);

    for (const auto id : {276U, 423U, 463U, 475U, 488U}) {
        assert(runtime.dependencies_ready('V', static_cast<std::uint16_t>(id)));
    }

    // Lane isolation: the binding cannot leak from lane0 into lane1.
    assert(spawn_lady_actor_effects(
        runtime, 0, 0x7FU, 0U, 0U, 1U, 4.0F) == 0U);
    assert(runtime.active_instances().empty());

    assert(spawn_lady_actor_effects(
        runtime, 0, 0x7FU, 1U, 0U, 1U, 4.0F) == 1U);
    assert(active_ids(runtime) == std::vector<std::uint32_t>{463U});
    assert(runtime.active_instances().front().source.resource_slot == 6U);
    assert(runtime.active_instances().front().source.transform_deferred);

    // Visibility is presentation-only.
    runtime.set_presentation_visible(false);
    Matrix4 exact;
    exact.values[12] = 12.0F;
    exact.values[13] = 34.0F;
    assert(spawn_lady_actor_effects(
        runtime, 2, 0x56U, 1U, 0U, 1U, 10.0F, &exact) == 1U);
    assert(!runtime.presentation_visible());
    assert(runtime.active_instances().back().active);
    assert(!runtime.active_instances().back().source.transform_deferred);
    assert(runtime.active_instances().back().source.world.values[12] == 12.0F);

    assert(spawn_lady_actor_effects(
        runtime, 4, 0x8FU, 1U, 0U, 1U, 45.0F) == 2U);
    assert(active_ids(runtime) ==
           (std::vector<std::uint32_t>{423U, 463U, 475U, 488U}));

    // Deterministic reset/replay: sequential 4->10->45 and replay-to-45
    // materialize the same roots; reverse seek is reset + replay to 10.
    const auto sequential_45 = active_ids(runtime);
    runtime.reset();
    assert(spawn_lady_actor_effects(
        runtime, 0, 0x7FU, 1U, 0U, 1U, 4.0F) == 1U);
    assert(spawn_lady_actor_effects(
        runtime, 2, 0x56U, 1U, 0U, 1U, 10.0F, &exact) == 1U);
    assert(spawn_lady_actor_effects(
        runtime, 4, 0x8FU, 1U, 0U, 1U, 45.0F) == 2U);
    assert(active_ids(runtime) == sequential_45);

    runtime.reset();
    assert(spawn_lady_actor_effects(
        runtime, 0, 0x7FU, 1U, 0U, 1U, 4.0F) == 1U);
    assert(spawn_lady_actor_effects(
        runtime, 2, 0x56U, 1U, 0U, 1U, 10.0F, &exact) == 1U);
    assert(active_ids(runtime) ==
           (std::vector<std::uint32_t>{423U, 463U}));

    // Missing child resource: root exists but automatic spawn is rejected.
    EffectRuntime incomplete;
    const auto missing = make_bank(true);
    assert(incomplete.load_bank(missing, 28U));
    assert(!incomplete.dependencies_ready('V', 463U));
    assert(spawn_lady_actor_effects(
        incomplete, 0, 0x7FU, 1U, 0U, 1U, 4.0F) == 0U);
    const auto rejected = incomplete.consume_effect_events();
    assert(rejected.size() == 1U);
    assert(rejected[0].kind == RuntimeEffectEventKind::Rejected);

    return 0;
}
