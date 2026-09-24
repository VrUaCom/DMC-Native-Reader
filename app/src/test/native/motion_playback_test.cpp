#include "dmcresource/motion/animated_local.h"
#include "dmcresource/motion/motion_player.h"
#include "dmcresource/motion/skeleton_rig.h"
#include "dmcresource/resource_session.h"

#include <bit>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numbers>
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

[[nodiscard]] bool near(float a, float b, float epsilon = 0.0005F) {
    return std::fabs(a - b) < epsilon;
}

}  // namespace

int main() {
    namespace motion = dmcresource::motion;

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

    // Frame 5: root translation-x = 15, vertices bound to bone 0 move +5.
    assert(motion::apply_motion_frame(session.get(), 5.0F));
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

    // 0x140310A61 binds only joints of the evaluated motion group: a MOT
    // covering the leading joints drives a model whose extra trailing joints
    // belong to another group (em000: 22-node MOTs on 23-node bodies).
    auto short_mot = make_translation_mot();
    put_u16(short_mot, 0x1CU, 2U);
    assert(!motion::load_motion(session.get(), "short.mot", short_mot.data(), short_mot.size()).ok);
    auto grouped_mod = make_spatial_mod();
    put_u8(grouped_mod, 0x229U, 2U);   // order position 1 = node 2 -> motion group 2
    auto grouped = dmcresource::open_session("grouped.mod", grouped_mod.data(), grouped_mod.size());
    assert(grouped && grouped->scene.rig && grouped->scene.rig->motion_group_by_node.size() == 3U &&
           grouped->scene.rig->motion_group_by_node[2] == 2U);
    const auto grouped_report =
        motion::load_motion(grouped.get(), "short.mot", short_mot.data(), short_mot.size());
    assert(grouped_report.ok && grouped_report.animated_parts == 1U);
    assert(motion::motion_can_drive(*grouped, short_mot));
    assert(!motion::motion_can_drive(*session, short_mot));
    return 0;
}
