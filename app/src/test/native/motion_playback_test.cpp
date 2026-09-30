#include "dmcresource/motion/animated_local.h"
#include "dmcresource/motion/motion_player.h"
#include "dmcresource/motion/skeleton_rig.h"
#include "dmcresource/resource_session.h"

#include <bit>
#include <array>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <numbers>
#include <string>
#include <vector>

// MOT playback regression: EXE 0x140310310 animated local, compression-2
// linear keys, animated world and inverseRest*world skinning on a synthetic
// three-node MOD. Fixture helpers mirror mod_spatial_adapter_test.cpp.
namespace {

void put_u8(std::vector<std::uint8_t>& bytes,
            std::size_t offset,
            std::uint8_t value) {
    assert(offset < bytes.size());
    bytes[offset] = value;
}

void put_u16(std::vector<std::uint8_t>& bytes,
             std::size_t offset,
             std::uint16_t value) {
    assert(offset + 2U <= bytes.size());
    put_u8(bytes, offset + 0U,
           static_cast<std::uint8_t>(value & 0xFFU));
    put_u8(bytes, offset + 1U,
           static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
}

void put_u32(std::vector<std::uint8_t>& bytes,
             std::size_t offset,
             std::uint32_t value) {
    assert(offset + 4U <= bytes.size());
    for (std::size_t i = 0U; i < 4U; ++i) {
        put_u8(bytes, offset + i,
               static_cast<std::uint8_t>(
                   (value >> (i * 8U)) & 0xFFU));
    }
}

void put_u64(std::vector<std::uint8_t>& bytes,
             std::size_t offset,
             std::uint64_t value) {
    assert(offset + 8U <= bytes.size());
    for (std::size_t i = 0U; i < 8U; ++i) {
        put_u8(bytes, offset + i,
               static_cast<std::uint8_t>(
                   (value >> (i * 8U)) & 0xFFU));
    }
}

void put_f32(std::vector<std::uint8_t>& bytes,
             std::size_t offset,
             float value) {
    put_u32(bytes, offset, std::bit_cast<std::uint32_t>(value));
}

void put_transform(std::vector<std::uint8_t>& bytes,
                   std::size_t offset,
                   float tx,
                   float ty,
                   float tz,
                   float magnitude) {
    put_f32(bytes, offset + 0x00U, tx);
    put_f32(bytes, offset + 0x04U, ty);
    put_f32(bytes, offset + 0x08U, tz);
    put_f32(bytes, offset + 0x0CU, magnitude);
    put_f32(bytes, offset + 0x10U, 0.0F);
    put_f32(bytes, offset + 0x14U, 0.0F);
    put_f32(bytes, offset + 0x18U, 0.0F);
    put_f32(bytes, offset + 0x1CU, 0.0F);
}

std::vector<std::uint8_t> make_spatial_mod() {
    std::vector<std::uint8_t> bytes(0x2A0U, 0U);
    bytes[0] = 'M';
    bytes[1] = 'O';
    bytes[2] = 'D';
    bytes[3] = ' ';
    put_f32(bytes, 0x04U, 1.01F);
    put_u8(bytes, 0x10U, 1U);
    put_u8(bytes, 0x11U, 3U);
    put_u64(bytes, 0x20U, 0x200U);

    // One outer model, one 3-vertex inner mesh.
    put_u8(bytes, 0x40U, 1U);
    put_u16(bytes, 0x42U, 3U);
    put_u64(bytes, 0x48U, 0x80U);

    put_u16(bytes, 0x80U, 3U);
    put_u16(bytes, 0x82U, 9U);
    put_u16(bytes, 0x84U, 1U);
    put_u16(bytes, 0x86U, 2U);
    put_u16(bytes, 0x88U, 3U);
    put_u16(bytes, 0x8AU, 4U);
    put_u64(bytes, 0x90U, 0xD0U);
    put_u64(bytes, 0x98U, 0x100U);
    put_u64(bytes, 0xA0U, 0x130U);
    put_u64(bytes, 0xA8U, 0x140U);
    put_u64(bytes, 0xB0U, 0x150U);
    put_u64(bytes, 0xB8U, 0U);
    put_u64(bytes, 0xC0U, 0xE0U);
    put_u32(bytes, 0xC8U, 0U);
    put_u32(bytes, 0xCCU, 0U);

    put_f32(bytes, 0xD0U, 0.0F);
    put_f32(bytes, 0xD4U, 0.0F);
    put_f32(bytes, 0xD8U, 0.0F);
    put_f32(bytes, 0xDCU, 1.0F);
    put_f32(bytes, 0xE0U, 0.0F);
    put_f32(bytes, 0xE4U, 0.0F);
    put_f32(bytes, 0xE8U, 0.0F);
    put_f32(bytes, 0xECU, 1.0F);
    put_f32(bytes, 0xF0U, 0.0F);

    for (std::size_t i = 0U; i < 3U; ++i) {
        const auto n = 0x100U + i * 12U;
        put_f32(bytes, n + 0U, 0.0F);
        put_f32(bytes, n + 4U, 0.0F);
        put_f32(bytes, n + 8U, 1.0F);
    }

    put_u16(bytes, 0x130U, 0U);
    put_u16(bytes, 0x132U, 0U);
    put_u16(bytes, 0x134U, 4096U);
    put_u16(bytes, 0x136U, 0U);
    put_u16(bytes, 0x138U, 0U);
    put_u16(bytes, 0x13AU, 4096U);

    // BLENDINDICES remain zero. Control values are a valid one-influence skin
    // encoding for bone 0 while the transform domain contains three nodes.
    put_u16(bytes, 0x150U, 0x001FU);
    put_u16(bytes, 0x152U, 0x001FU);
    put_u16(bytes, 0x154U, 0x001FU);

    // Canonical node-domain shell for count=3:
    // parent +0x20, order +0x24, adapter +0x28, transforms +0x30.
    put_u32(bytes, 0x200U, 0x20U);
    put_u32(bytes, 0x204U, 0x24U);
    put_u32(bytes, 0x208U, 0x28U);
    put_u32(bytes, 0x20CU, 0x30U);

    // Non-linear evaluation order proves parent values are node indices:
    // root node0 -> node2 -> node1.
    put_u8(bytes, 0x220U, 0xFFU);
    put_u8(bytes, 0x221U, 0U);
    put_u8(bytes, 0x222U, 2U);

    put_u8(bytes, 0x224U, 0U);
    put_u8(bytes, 0x225U, 2U);
    put_u8(bytes, 0x226U, 1U);

    put_u8(bytes, 0x228U, 0U);
    put_u8(bytes, 0x229U, 0U);
    put_u8(bytes, 0x22AU, 0U);

    put_transform(bytes, 0x230U, 10.0F, 0.0F, 0.0F, 10.0F);
    put_transform(bytes, 0x250U, 0.0F, 0.0F, 2.0F, 2.0F);
    put_transform(bytes, 0x270U, 0.0F, 5.0F, 0.0F, 5.0F);
    return bytes;
}

std::vector<std::uint8_t> make_lady_body_mod() {
    auto bytes = make_spatial_mod();
    bytes.resize(0x580U, 0U);
    put_u8(bytes, 0x11U, 23U);
    put_u64(bytes, 0x20U, 0x200U);

    // The synthetic body only needs one triangle, but it retains a full
    // 23-node spatial domain so the recovered component presets can address
    // host joints 3 and 9 without weakening the production attachment gate.
    put_u32(bytes, 0x200U, 0x20U);
    put_u32(bytes, 0x204U, 0x40U);
    put_u32(bytes, 0x208U, 0x60U);
    put_u32(bytes, 0x20CU, 0x80U);
    for (std::size_t node = 0U; node < 23U; ++node) {
        put_u8(bytes, 0x220U + node,
                node == 0U ? 0xFFU : static_cast<std::uint8_t>(node - 1U));
        put_u8(bytes, 0x240U + node, static_cast<std::uint8_t>(node));
        put_u8(bytes, 0x260U + node, 0U);
        put_transform(bytes, 0x280U + node * 0x20U,
                      0.0F, node == 0U ? 0.0F : 1.0F, 0.0F,
                      node == 0U ? 0.0F : 1.0F);
    }
    return bytes;
}

// Three-node MOT: node 0 translation-x only, one compression-2 track with
// keys (frame 0 -> 10.0) and (frame 10 -> 20.0).
std::vector<std::uint8_t> make_translation_mot() {
    std::vector<std::uint8_t> bytes(0x50U, 0U);
    put_u32(bytes, 0x00U, 0x30U);
    bytes[4] = 'M';
    bytes[5] = 'O';
    bytes[6] = 'T';
    bytes[7] = 0;
    put_f32(bytes, 0x0CU, 10.0F);
    put_f32(bytes, 0x14U, 10.0F);
    put_u16(bytes, 0x1CU, 3U);
    put_u16(bytes, 0x1EU, 0x040U);
    put_u32(bytes, 0x30U, 1U);
    put_u16(bytes, 0x34U, 0x18U);
    put_u16(bytes, 0x36U, 2U);
    put_u16(bytes, 0x38U, 2U);
    put_u16(bytes, 0x3AU, 0U);
    put_f32(bytes, 0x3CU, 10.0F);
    put_f32(bytes, 0x40U, 10.0F);
    put_u16(bytes, 0x44U, 0U);
    put_u16(bytes, 0x46U, 0U);
    put_u16(bytes, 0x48U, 10U);
    put_u16(bytes, 0x4AU, 0xFFFFU);
    return bytes;
}

// Same translation track with a full 23-node body header.
std::vector<std::uint8_t> make_translation_mot_domain23() {
    std::vector<std::uint8_t> bytes(0x70U, 0U);
    put_u32(bytes, 0x00U, 0x50U);
    bytes[4] = 'M';
    bytes[5] = 'O';
    bytes[6] = 'T';
    bytes[7] = 0;
    put_f32(bytes, 0x0CU, 10.0F);
    put_f32(bytes, 0x14U, 10.0F);
    put_u16(bytes, 0x1CU, 23U);
    put_u16(bytes, 0x1EU, 0x040U);

    put_u32(bytes, 0x50U, 1U);
    put_u16(bytes, 0x54U, 0x18U);
    put_u16(bytes, 0x56U, 2U);
    put_u16(bytes, 0x58U, 2U);
    put_u16(bytes, 0x5AU, 0U);
    put_f32(bytes, 0x5CU, 10.0F);
    put_f32(bytes, 0x60U, 10.0F);
    put_u16(bytes, 0x64U, 0U);
    put_u16(bytes, 0x66U, 0U);
    put_u16(bytes, 0x68U, 10U);
    put_u16(bytes, 0x6AU, 0xFFFFU);
    return bytes;
}

// Enemy-form MotionScript with five banks and 45 actions per bank. Every
// action resolves to the same tiny script; bank4/action44 is the canonical
// synthetic Lady state-0x7F route used below. The opcode-3 payload is a
// five-channel row whose lane1 channel0 value is 1, so the CEm034 bridge can
// be exercised without naming or guessing an effect frame.
std::vector<std::uint8_t> make_lady_runtime_script() {
    constexpr std::size_t kTable = 6U;
    constexpr std::size_t kBankCount = 5U;
    constexpr std::size_t kActionCount = 45U;
    constexpr std::size_t kBankListBytes = kBankCount * 2U + 2U;
    constexpr std::size_t kActionListBytes = kActionCount * 2U + 2U;
    constexpr std::size_t kScriptBytes = 8U + 6U + 6U + 6U;

    std::vector<std::uint8_t> bytes(kTable + kBankListBytes, 0U);
    put_u16(bytes, 0U, static_cast<std::uint16_t>(kTable));
    put_u16(bytes, 2U, 0U);  // no resource table: the test supplies ScriptLink
    put_u16(bytes, 4U, 0xFFFFU);

    std::array<std::size_t, kBankCount> bank_lists{};
    for (std::size_t bank = 0U; bank < kBankCount; ++bank) {
        bank_lists[bank] = bytes.size();
        bytes.resize(bytes.size() + kActionListBytes, 0U);
        put_u16(bytes, kTable + bank * 2U,
                static_cast<std::uint16_t>(bank_lists[bank] - kTable));
    }
    put_u16(bytes, kTable + kBankCount * 2U, 0xFFFFU);

    const std::size_t script = bytes.size();
    bytes.resize(bytes.size() + kScriptBytes, 0U);
    bytes[script + 0U] = 1U;       // play MOT
    bytes[script + 4U] = 4U;       // bank metadata, not the runtime link
    bytes[script + 5U] = 44U;
    bytes[script + 8U] = 3U;       // opcode3: five channels
    bytes[script + 9U] = 1U;       // channel0 == 1 on the lane that consumes it
    bytes[script + 14U] = 0U;      // wait to frame 10
    put_u16(bytes, script + 16U, 10U);
    bytes[script + 20U] = 0U;      // terminal wait
    put_u16(bytes, script + 22U, 0x7FFFU);

    for (const auto list : bank_lists) {
        const auto relative = static_cast<std::uint16_t>(script - list);
        for (std::size_t action = 0U; action < kActionCount; ++action) {
            put_u16(bytes, list + action * 2U, relative);
        }
        put_u16(bytes, list + kActionCount * 2U, 0xFFFFU);
    }
    return bytes;
}

[[nodiscard]] bool near(float a, float b, float epsilon = 0.0005F) {
    return std::fabs(a - b) < epsilon;
}

std::size_t g_generic_bridge_resets{};

void generic_profile_effect_reset(dmcresource::Session*) noexcept {
    ++g_generic_bridge_resets;
}

void generic_profile_effect_step(dmcresource::Session* session,
                                 float frame) noexcept {
    if (session == nullptr || session->effect_runtime == nullptr) return;
    dmcresource::motion::DynamicActorEvent event;
    event.actor = 7U;
    event.actor_state = 4U;
    event.lane = 0U;
    event.channel = 2U;
    event.signal_value = 3U;
    event.script_frame = frame;
    event.world_authoritative = true;
    event.evidence = dmcresource::motion::EvidenceStatus::EXE_AND_CORPUS_CONFIRMED;
    const auto instances = session->effect_runtime->instances();
    if (instances.empty()) {
        event.kind = dmcresource::motion::DynamicActorEventKind::Spawn;
        event.actor_instance = 1U;
    } else {
        event.kind = dmcresource::motion::DynamicActorEventKind::Update;
        event.actor_instance = instances.front().actor_instance;
    }
    session->effect_runtime->apply_actor_event(event);
}

}  // namespace

int main() {
    namespace motion = dmcresource::motion;

    // Retail 0x1402e7a90 mode=3 normalizes the selected slot20
    // orientation rows for V423 independently of the Shl02 actor basis.
    {
        dmcresource::Matrix4 raw;
        raw.values = {
            0.0F, 2.0F, 0.0F, 0.0F,
            -3.0F, 0.0F, 0.0F, 0.0F,
            0.0F, 0.0F, 4.0F, 0.0F,
            11.0F, 12.0F, 13.0F, 1.0F,
        };
        const auto effect_parent = motion::shl02_effect_parent_matrix(raw);
        assert(near(effect_parent.values[0], 0.0F));
        assert(near(effect_parent.values[1], 1.0F));
        assert(near(effect_parent.values[4], -1.0F));
        assert(near(effect_parent.values[10], 1.0F));
        assert(effect_parent.values[12] == 11.0F &&
               effect_parent.values[13] == 12.0F &&
               effect_parent.values[14] == 13.0F);
    }

    // CEm034Shl02: the flight direction is the slot20 X axis only. The old
    // Reader multiplied (1,0,0,1) by the full matrix, adding the hand
    // translation (~y 100) to the direction and pointing the shell upward.
    {
        dmcresource::Matrix4 slot20;
        slot20.values = {
            0.0F, 0.0F, 2.0F, 0.0F,   // X axis -> world +Z (scaled)
            0.0F, 1.0F, 0.0F, 0.0F,
            -1.0F, 0.0F, 0.0F, 0.0F,
            5.0F, 100.0F, -7.0F, 1.0F,
        };
        const auto at0 = motion::shl02_shell_world(slot20, 0.0F);
        // Init 0x1401738F0 adds (18.6,0,12) in world axes.
        assert(near(at0.values[12], 23.6F) && near(at0.values[13], 100.0F) &&
               near(at0.values[14], 5.0F));
        // Align-Z: row2 = flight direction, row1 = up, row0 = up x dir.
        assert(near(at0.values[8], 0.0F) && near(at0.values[9], 0.0F) &&
               near(at0.values[10], 1.0F));
        assert(near(at0.values[5], 1.0F));
        assert(near(at0.values[0], 1.0F));
        // 30 units per tick, straight until the first retarget.
        const auto at9 = motion::shl02_shell_world(slot20, 9.0F);
        assert(near(at9.values[14], 5.0F + 270.0F, 0.01F) &&
               near(at9.values[13], 100.0F));
        // Lifetime 120 ticks: the position holds from there (explode state).
        const auto at120 = motion::shl02_shell_world(slot20, 120.0F);
        const auto at200 = motion::shl02_shell_world(slot20, 200.0F);
        assert(near(at120.values[14], 5.0F + 3600.0F, 0.05F));
        assert(at200.values[14] == at120.values[14]);
    }

    // 16-bit angle wrap exactly as cvttss2si + word store.
    assert(near(motion::quantize_motion_angle(0.5F), 0.5F, 0.0002F));
    assert(near(motion::quantize_motion_angle(2.0F * std::numbers::pi_v<float> + 0.5F),
                0.5F, 0.0005F));
    assert(near(motion::quantize_motion_angle(1.5F * std::numbers::pi_v<float>),
                -0.5F * std::numbers::pi_v<float>, 0.0005F));
    assert(motion::quantize_motion_angle(std::numeric_limits<float>::quiet_NaN()) == 0.0F);

    motion::JointChannels channels;
    channels.translation = {1.0F, 2.0F, 3.0F};
    const auto local = motion::build_animated_local_matrix(channels);
    assert(local.values[0] == 1.0F && local.values[5] == 1.0F && local.values[10] == 1.0F);
    assert(local.values[12] == 1.0F && local.values[13] == 2.0F && local.values[14] == 3.0F);
    assert(local.values[15] == 1.0F);
    assert(!motion::has_non_unit_scale(channels));
    channels.scale = {2.0F, 1.0F, 1.0F};
    assert(motion::has_non_unit_scale(channels));
    assert(motion::build_animated_local_matrix(channels).values[0] == 2.0F);

    const auto mod = make_spatial_mod();
    auto session = dmcresource::open_session("synthetic.mod", mod.data(), mod.size());
    assert(session != nullptr);
    assert(session->scene.rig != nullptr);
    assert(session->scene.rig->node_count() == 3U);
    const auto rest = session->render_mesh.vertices;
    assert(rest.size() == 3U);
    const float rest_root_x = session->scene.nodes[0].world.values[12];
    assert(near(rest_root_x, 10.0F));

    const auto mot = make_translation_mot();
    const auto report = motion::load_motion(session.get(), "synthetic.mot", mot.data(), mot.size());
    assert(report.ok);
    assert(report.animated_parts == 1U);
    assert(report.end_frame == 10.0F);
    assert(motion::has_motion(session.get()));
    // Raw MOT is intentionally isolated from Script Play runtime effects.
    assert(motion::effect_events(session.get()).empty());

    // Frame 5: root translation-x = 15, vertices bound to bone 0 move +5.
    assert(motion::apply_motion_frame(session.get(), 5.0F));
    assert(motion::effect_events(session.get()).empty());
    assert(near(session->scene.nodes[0].world.values[12], 15.0F));
    for (std::size_t i = 0U; i < rest.size(); ++i) {
        assert(near(session->render_mesh.vertices[i].x, rest[i].x + 5.0F));
        assert(near(session->render_mesh.vertices[i].y, rest[i].y));
        assert(near(session->render_mesh.vertices[i].z, rest[i].z));
    }
    // Children inherit the animated parent world.
    assert(near(session->scene.nodes[2].world.values[12], 15.0F));

    // Past the last key holds the last value; backwards seeks use the cache.
    assert(motion::apply_motion_frame(session.get(), 50.0F));
    assert(near(session->scene.nodes[0].world.values[12], 20.0F));
    assert(motion::apply_motion_frame(session.get(), 2.5F));
    assert(near(session->scene.nodes[0].world.values[12], 12.5F));

    // Camera framing stays on the rest pose while posed; rendering still works.
    assert(motion::motion_rest_vertices(session.get()).size() == rest.size());
    const auto frame_image = dmcresource::render_session(
        session.get(), 96, 96, 0.6F, -0.4F, 1.0F, 0U);
    assert(frame_image.width == 96 && frame_image.height == 96);

    // Clearing restores the source pose byte-for-byte.
    motion::clear_motion(session.get());
    assert(!motion::has_motion(session.get()));
    assert(motion::motion_rest_vertices(session.get()).empty());
    for (std::size_t i = 0U; i < rest.size(); ++i) {
        assert(session->render_mesh.vertices[i].x == rest[i].x);
    }
    assert(session->scene.nodes[0].world.values[12] == rest_root_x);

    // A MOT for a different skeleton size never animates this model.
    auto wrong = make_translation_mot();
    put_u16(wrong, 0x1CU, 4U);
    const auto rejected = motion::load_motion(session.get(), "wrong.mot", wrong.data(), wrong.size());
    assert(!rejected.ok);
    assert(!motion::has_motion(session.get()));

    // 0x140310A61 consumes one mask per initialized CMotion joint. A shorter
    // declared MOT domain is valid when the aligned header tail contains zero
    // masks for the remaining model nodes (the em034 slot20 2-mask/3-node
    // controller is the canonical real-resource case).
    auto short_mot = make_translation_mot();
    put_u16(short_mot, 0x1CU, 2U);
    assert(motion::load_motion(session.get(), "short.mot", short_mot.data(), short_mot.size()).ok);
    assert(near(session->scene.nodes[0].world.values[12], 10.0F));
    assert(near(session->scene.nodes[2].world.values[12], 10.0F));
    auto grouped_mod = make_spatial_mod();
    put_u8(grouped_mod, 0x229U, 2U);   // order position 1 = node 2 -> motion group 2
    auto grouped = dmcresource::open_session("grouped.mod", grouped_mod.data(), grouped_mod.size());
    assert(grouped && grouped->scene.rig && grouped->scene.rig->motion_group_by_node.size() == 3U &&
           grouped->scene.rig->motion_group_by_node[2] == 2U);
    const auto grouped_report =
        motion::load_motion(grouped.get(), "short.mot", short_mot.data(), short_mot.size());
    assert(grouped_report.ok && grouped_report.animated_parts == 1U);
    assert(motion::motion_can_drive(*grouped, short_mot));
    assert(motion::motion_can_drive(*session, short_mot));

    // A non-zero byte in the aligned mask tail is not silently promoted to a
    // synthetic domain entry; the bounded compatibility rule fails closed.
    auto nonzero_padding = short_mot;
    put_u16(nonzero_padding, 0x22U, 0x040U);
    assert(!motion::load_motion(
        session.get(), "nonzero-padding.mot",
        nonzero_padding.data(), nonzero_padding.size()).ok);

    // Integrated Script Play: controller -> bank4/action44 -> CEm034 state
    // 0x7F -> lane1/channel0 -> Shl00 -> V463. The synthetic session has no
    // exact Shl00 actor matrix, so the effect must remain deferred rather than
    // appearing at identity. This still exercises the real MotionPlayer
    // bridge, resource gate and deterministic reverse replay.
    {
        auto lady = dmcresource::open_session(
            "em034-body.mod", mod.data(), mod.size());
        assert(lady != nullptr);
        lady->archive_name = "em034.pac";
        lady->effect_bank_slots = {28U};
        const auto append_resource = [&effect_resources = lady->effect_resources](
                                         char kind,
                                         std::uint16_t id,
                                         std::uint32_t slot,
                                         motion::EvidenceStatus evidence) {
            effect_resources.push_back({kind, id, slot, evidence});
        };
        const auto append_child_resources =
            [&append_resource](const auto& self,
                               const motion::EffectChildRef& child) -> void {
                append_resource(child.effect_kind, child.effect_id,
                                child.resource_slot, child.evidence);
                for (const auto& nested : child.children) self(self, nested);
            };
        for (const auto& profile : motion::em034_effect_bindings()) {
            append_resource(profile.effect_kind, profile.effect_id,
                            profile.resource_slot,
                            motion::EvidenceStatus::EXE_AND_CORPUS_CONFIRMED);
            for (const auto& child : profile.children) {
                append_child_resources(append_child_resources, child);
            }
        }
        lady->script_effect_bridge.prepare = motion::install_effect_bindings;
        lady->lady_component_bindings.push_back({
            0U, 0U, 0U, 20U, motion::LadyPlacementPreset::BodyStowed,
            motion::LadyControlDomain::BodyConstraint, 1.0F});

        const auto script_bytes = make_lady_runtime_script();
        const auto script = motion::MotionScriptFile::parse(script_bytes);
        assert(script && script->bank_count() == 5U);
        assert(script->signals(4U, 44U).size() == 1U);
        lady->motion_scripts.push_back({
            12U, dmcresource::Session::MotionScriptRole::LadyBody,
            std::make_shared<const motion::MotionScriptFile>(*script)});
        lady->motion_library.push_back({});
        auto& payload = lady->motion_library.back();
        payload.name = "synthetic-lady.mot";
        payload.bytes = make_translation_mot();
        payload.script_links.push_back({0U, 4U, 44U, 0x7F, 0x3U});

        const auto first = motion::run_script_frame(
            lady.get(), 0U, motion::ScriptActionId{4U, 44U, 0U}, 0.0F);
        assert(first.actor_events.size() == 1U);
        assert(first.actor_events[0].actor == 0U);
        assert(first.actor_events[0].lane == 1U &&
               first.actor_events[0].channel == 0U &&
               first.actor_events[0].signal_value == 1U);
        assert(first.effect_events.size() == 1U);
        assert(first.effect_events[0].kind ==
               motion::RuntimeEffectEvent::Kind::Deferred);
        assert(first.effect_events[0].instance.source.effect_kind == 'V');
        assert(first.effect_events[0].instance.source.effect_id == 463U);
        assert(first.effect_events[0].instance.state ==
               motion::EffectRuntimeState::DeferredTransform);
        assert(motion::active_effect_instances(lady.get()).empty());
        assert(lady->effect_runtime != nullptr &&
               lady->effect_runtime->instances().size() == 1U);

        const auto sequential = motion::run_script_frame(
            lady.get(), 0U, motion::ScriptActionId{4U, 44U, 0U}, 45.0F);
        assert(sequential.effect_events.empty());
        assert(lady->effect_runtime->instances().size() == 1U);
        assert(lady->effect_runtime->instances()[0].state ==
               motion::EffectRuntimeState::DeferredTransform);

        motion::set_effects_visible(lady.get(), false);
        assert(lady->effect_runtime->instances().size() == 1U);
        assert(lady->effect_runtime->presentation_instances().empty());
        assert(motion::presentation_effect_instances(lady.get()).empty());
        motion::set_effects_visible(lady.get(), true);

        const auto reverse = motion::run_script_frame(
            lady.get(), 0U, motion::ScriptActionId{4U, 44U, 0U}, 10.0F);
        assert(reverse.effect_events.size() == 1U);
        assert(reverse.effect_events[0].kind ==
               motion::RuntimeEffectEvent::Kind::Deferred);
        assert(reverse.effect_events[0].instance.instance_id == 1U);
        assert(reverse.effect_events[0].instance.source.effect_id == 463U);
        assert(lady->effect_runtime->instances().size() == 1U);
        assert(lady->effect_runtime->instances()[0].current_frame == 0.0F);
    }

    // Confirmed synchronized Lady pair: the body controller and component0
    // controller own separate MOD parts but are evaluated at one Script Play
    // frame. This is the bounded em034 bank4/action3..5 integration slice;
    // no unverified controller/resource pairing is introduced here.
    {
        const auto lady_body_mod = make_lady_body_mod();
        auto body_source = dmcresource::open_session(
            "em034-body.mod", lady_body_mod.data(), lady_body_mod.size());
        auto component_source = dmcresource::open_session(
            "em034-component.mod", mod.data(), mod.size());
        assert(body_source != nullptr && component_source != nullptr);
        const std::vector<const dmcresource::Session*> parts{
            body_source.get(), component_source.get()};
        const std::vector<std::string> names{"Lady body", "Lady component0"};
        auto lady_pair = dmcresource::compose_mod_sessions(parts, names);
        assert(lady_pair != nullptr && lady_pair->composite_parts.size() == 2U);
        const auto body_mot23 = make_translation_mot_domain23();
        const auto raw_body = motion::load_motion(
            body_source.get(), "synthetic-body-domain23.mot",
            body_mot23.data(), body_mot23.size());
        assert(raw_body.ok);
        lady_pair->archive_name = "em034.pac";
        lady_pair->lady_component_bindings.push_back({
            0U, 1U, 0U, 20U, motion::LadyPlacementPreset::BodyStowed,
            motion::LadyControlDomain::BodyConstraint, 1.0F});
        auto& component_binding = lady_pair->lady_component_bindings.front();
        assert(motion::set_lady_component_preset(
            lady_pair.get(), component_binding,
            motion::LadyPlacementPreset::BodyStowed));

        const auto script_bytes = make_lady_runtime_script();
        const auto script = motion::MotionScriptFile::parse(script_bytes);
        assert(script.has_value());
        const auto shared_script =
            std::make_shared<const motion::MotionScriptFile>(*script);
        lady_pair->motion_scripts.push_back({
            12U, dmcresource::Session::MotionScriptRole::LadyBody,
            shared_script});
        lady_pair->motion_scripts.push_back({
            13U, dmcresource::Session::MotionScriptRole::LadyComponent0,
            shared_script});

        lady_pair->motion_library.resize(2U);
        lady_pair->motion_library[0].name = "synthetic-lady-body.mot";
        lady_pair->motion_library[0].bytes = body_mot23;
        lady_pair->motion_library[0].script_links.push_back({
            0U, 4U, 3U, -1, 0U});
        lady_pair->motion_library[1].name = "synthetic-lady-component0.mot";
        lady_pair->motion_library[1].bytes = make_translation_mot();
        lady_pair->motion_library[1].script_links.push_back({
            1U, 4U, 3U, -1, 0U});

        // The legacy load entry used by the JNI/UI shell must promote the
        // same confirmed pair; this guards the integration boundary separately
        // from the frame-step API below.
        const auto direct = motion::load_scripted_motion(
            lady_pair.get(), 0U, 0U);
        assert(direct.ok);
        assert(direct.synchronized_tracks == 2U);
        assert(direct.deferred_tracks == 0U);

        const auto legacy_step = motion::run_script_frame(
            lady_pair.get(), 0U, motion::ScriptActionId{4U, 3U, 0U}, 0.0F);
        assert(legacy_step.synchronized_tracks == 2U);
        assert(legacy_step.deferred_tracks == 0U);

        const std::array<motion::ScriptTrackAction, 2> tracks{{
            {0U, motion::ScriptActionId{4U, 3U, 0U}},
            {1U, motion::ScriptActionId{
                4U, 3U, std::numeric_limits<std::size_t>::max()}},
        }};
        const auto first = motion::run_synchronized_script_frame(
            lady_pair.get(), tracks, 0.0F);
        assert(first.synchronized_tracks == 2U);
        assert(first.deferred_tracks == 0U);
        assert(motion::has_motion(lady_pair.get()));
        assert(component_binding.control_domain ==
               motion::LadyControlDomain::IndependentMotionScript);
        assert(lady_pair->composite_parts[1].placement.mode ==
               dmcresource::CompositePlacementMode::HostJoint);

        const float body_root_at_zero =
            lady_pair->scene.nodes[0].world.values[12];
        const float component_root_at_zero =
            lady_pair->scene.nodes[23].world.values[12];
        const auto forward = motion::run_synchronized_script_frame(
            lady_pair.get(), tracks, 5.0F);
        assert(forward.synchronized_tracks == 2U);
        assert(forward.deferred_tracks == 0U);
        assert(near(lady_pair->scene.nodes[0].world.values[12],
                    body_root_at_zero + 5.0F));
        assert(near(lady_pair->scene.nodes[23].world.values[12],
                    component_root_at_zero + 5.0F));
    }

    // Profile-neutral ScriptEffectBridge: a non-Lady character can emit its
    // own evidence-backed actor events through the same runtime. Reverse seek
    // resets and replays the effect timeline, while raw MOT stays isolated.
    {
        dmcresource::Session unknown_profile;
        unknown_profile.archive_name = "em999.pac";
        unknown_profile.effect_bank_slots = {28U};
        assert(!motion::install_effect_bindings(&unknown_profile));
        assert(unknown_profile.script_effect_bindings.empty());

        auto generic = dmcresource::open_session(
            "em999-body.mod", mod.data(), mod.size());
        assert(generic != nullptr);
        constexpr motion::EffectBinding binding{
            7U, 'P', 18U, 41U, motion::RuntimeEffectParent::DynamicActor,
            4U, 0U, 2U, 3U,
            motion::EvidenceStatus::EXE_AND_CORPUS_CONFIRMED,
            motion::EffectLifetimeRule::ParentActorRetire,
            motion::EvidenceStatus::EXE_CONFIRMED};
        generic->effect_resources.push_back(
            {'P', 18U, 41U, motion::EvidenceStatus::EXE_AND_CORPUS_CONFIRMED});
        assert(motion::set_script_effect_bindings(
            generic.get(), std::span<const motion::EffectBinding>{&binding, 1U}));
        generic->script_effect_bridge = {
            nullptr, generic_profile_effect_reset, generic_profile_effect_step};
        assert(motion::ensure_effect_runtime(generic.get()));

        const auto script_bytes = make_lady_runtime_script();
        const auto script = motion::MotionScriptFile::parse(script_bytes);
        assert(script.has_value());
        generic->motion_scripts.push_back({
            38U, dmcresource::Session::MotionScriptRole::Primary,
            std::make_shared<const motion::MotionScriptFile>(*script)});
        generic->motion_library.push_back({});
        auto& payload = generic->motion_library.back();
        payload.name = "synthetic-generic.mot";
        payload.bytes = make_translation_mot();
        payload.script_links.push_back({0U, 4U, 44U, -1, 0U});

        const auto first = motion::run_script_frame(
            generic.get(), 0U, motion::ScriptActionId{4U, 44U, 0U}, 0.0F);
        assert(first.effect_events.size() == 1U);
        assert(first.effect_events[0].kind == motion::RuntimeEffectEvent::Kind::Spawn);
        assert(first.effect_events[0].instance.source.effect_id == 18U);

        const auto forward = motion::run_script_frame(
            generic.get(), 0U, motion::ScriptActionId{4U, 44U, 0U}, 10.0F);
        assert(forward.effect_events.size() == 1U);
        assert(forward.effect_events[0].kind == motion::RuntimeEffectEvent::Kind::Update);
        assert(generic->effect_runtime->instances()[0].current_frame == 10.0F);

        const auto reverse = motion::run_script_frame(
            generic.get(), 0U, motion::ScriptActionId{4U, 44U, 0U}, 2.0F);
        assert(reverse.effect_events.size() == 1U);
        assert(reverse.effect_events[0].kind == motion::RuntimeEffectEvent::Kind::Spawn);
        assert(generic->effect_runtime->instances()[0].current_frame == 2.0F);
        assert(g_generic_bridge_resets > 0U);

        assert(motion::load_library_motion(generic.get(), 0U).ok);
        assert(motion::effect_events(generic.get()).empty());
    }
    return 0;
}
