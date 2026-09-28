#include <algorithm>
#include "dmcresource/motion/motion_player.h"
#include "dmcresource/pac_assembly.h"
#include "dmcresource/resource_session.h"

#include <array>
#include <bit>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// PAC regression: relative-slot archive -> typed children -> per-slot opening
// -> read-only assembly (MODs in model space, MOT library, nested PAC).
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

    // 23-node canonical MOD transform-domain shell. A simple chain is enough
    // for the assembly regression: the test verifies exact CEm034 attachment
    // selectors/offsets, not the retail Lady rest pose itself.
    put_u32(bytes, 0x200U, 0x20U);  // parent[23]
    put_u32(bytes, 0x204U, 0x40U);  // order[23]
    put_u32(bytes, 0x208U, 0x60U);  // adapter[23]
    put_u32(bytes, 0x20CU, 0x80U);  // transforms[23]

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

std::vector<std::uint8_t> make_pac(const std::vector<std::vector<std::uint8_t>>& payloads) {
    std::size_t cursor = 8U + payloads.size() * 4U;
    cursor = (cursor + 0x0FU) & ~std::size_t{0x0FU};
    std::vector<std::uint8_t> bytes(cursor, 0U);
    bytes[0] = 'P';
    bytes[1] = 'A';
    bytes[2] = 'C';
    bytes[3] = 0U;
    put_u32(bytes, 4U, static_cast<std::uint32_t>(payloads.size()));
    for (std::size_t index = 0U; index < payloads.size(); ++index) {
        put_u32(bytes, 8U + index * 4U, static_cast<std::uint32_t>(bytes.size()));
        bytes.insert(bytes.end(), payloads[index].begin(), payloads[index].end());
        bytes.resize((bytes.size() + 0x0FU) & ~std::size_t{0x0FU}, 0U);
    }
    return bytes;
}

}  // namespace

int main() {
    namespace assembly = dmcresource::pac_assembly;
    const auto mod = make_spatial_mod();
    const auto mot = make_translation_mot();
    const std::vector<std::uint8_t> opaque(32U, 0xABU);

    // Single-model PAC with a motion and an unknown payload.
    const auto pac = make_pac({mod, mot, opaque});
    auto archive = dmcresource::open_session("pl_test.pac", pac.data(), pac.size());
    assert(archive != nullptr);
    assert(archive->probe.format == dmcresource::Format::Pac);
    assert(!archive->renderable);
    assert(archive->children.size() == 3U);
    assert(archive->children[0].suggested_filename == "slot_0000.mod");
    assert(archive->children[1].suggested_filename == "slot_0001.mot");
    assert(archive->children[2].suggested_filename == "slot_0002.bin");

    // Per-file display: each recognized slot opens in its own module.
    auto slot_mod = dmcresource::open_session_child(archive.get(), 0);
    assert(slot_mod != nullptr && slot_mod->renderable);
    auto slot_mot = dmcresource::open_session_child(archive.get(), 1);
    assert(slot_mot != nullptr && slot_mot->probe.format == dmcresource::Format::Mot);
    assert(slot_mot->detail.find("nodes=3") != std::string::npos);

    assembly::AssemblyReport report;
    auto single = assembly::assemble_pac(*archive, &report);
    assert(single != nullptr);
    assert(report.models == 1U && report.motions == 1U);
    assert(single->renderable);
    assert(single->motion_library.size() == 1U);
    assert(single->children.size() == 3U);
    const auto& motion = single->motion_library.front();
    const auto loaded = dmcresource::motion::load_motion(
        single.get(), motion.name, motion.bytes.data(), motion.bytes.size());
    assert(loaded.ok);

    // Nested PAC with two MODs: composite keeps both parts in model space.
    const auto inner = make_pac({mod, mot});
    const auto outer = make_pac({mod, inner});
    auto nested = dmcresource::open_session("em_test.pac", outer.data(), outer.size());
    assert(nested != nullptr);
    auto composite = assembly::assemble_pac(*nested, &report);
    assert(composite != nullptr);
    assert(report.models == 2U && report.nested_archives == 1U && report.motions == 1U);
    assert(composite->composite_parts.size() == 2U);
    assert(!composite->composite_parts[1].placement.resolved);
    const auto& base = slot_mod->render_mesh.vertices;
    for (std::size_t i = 0U; i < base.size(); ++i) {
        assert(composite->render_mesh.vertices[base.size() + i].x == base[i].x);
        assert(composite->render_mesh.vertices[base.size() + i].y == base[i].y);
    }
    // One motion drives both parts because both share the skeleton size.
    const auto& nested_motion = composite->motion_library.front();
    const auto both = dmcresource::motion::load_motion(
        composite.get(), nested_motion.name,
        nested_motion.bytes.data(), nested_motion.bytes.size());
    assert(both.ok && both.animated_parts == 2U);

    // em034.pac: body/hair + five persistent CEm034 components.
    // Dynamic Shl resources (25/26/30) stay browsable but must not be overlaid.
    const auto lady_body = make_lady_body_mod();
    std::vector<std::vector<std::uint8_t>> lady_payloads(35U);
    lady_payloads[1] = lady_body;
    lady_payloads[17] = mod;
    for (const std::size_t slot : {20U, 21U, 22U, 23U, 24U}) {
        lady_payloads[slot] = mod;
    }
    for (const std::size_t slot : {25U, 26U, 30U}) {
        // Valid MOD payloads so the assembly can retain them as latent Shl
        // visual sources while keeping them out of the persistent composite.
        lady_payloads[slot] = mod;
    }
    lady_payloads[32] = lady_body;
    lady_payloads[34] = mod;
    const auto lady_pac = make_pac(lady_payloads);
    auto lady = dmcresource::open_session(
        "em034.pac", lady_pac.data(), lady_pac.size());
    assert(lady != nullptr);

    const auto lady_variants = dmcresource::motion::archive_variants("em034.pac");
    assert(lady_variants.size() == 2U);
    assert(lady_variants[0].label == "Lady · costume 1");
    assert(lady_variants[1].label == "Lady · costume 2");
    assert(lady_variants[0].include_top_level_mod_count == 7U);
    assert(lady_variants[0].texture_override_count == 7U);
    assert(lady_variants[0].part_attachment_count == 6U);

    // Hair remains structural; persistent equipment is now exact EXE evidence.
    assert(lady_variants[0].part_attachments[0].child_model_slot == 17U);
    assert(lady_variants[0].part_attachments[0].host_joint == 5U);
    assert(lady_variants[0].part_attachments[0].structural_confirmed);
    assert(!lady_variants[0].part_attachments[0].exe_confirmed);

    const std::array<std::uint32_t, 5> expected_slots{20U, 21U, 22U, 23U, 24U};
    const std::array<std::uint32_t, 5> expected_stowed_joints{3U, 14U, 16U, 19U, 14U};
    for (std::size_t i = 0U; i < expected_slots.size(); ++i) {
        const auto& a = lady_variants[0].part_attachments[i + 1U];
        assert(a.child_model_slot == expected_slots[i]);
        assert(a.host_joint == expected_stowed_joints[i]);
        assert(a.structural_confirmed);
        assert(a.exe_confirmed);
        assert(a.explicit_offset);
    }
    assert(lady_variants[0].part_attachments[1].translation[0] == -2.0F);
    assert(lady_variants[0].part_attachments[1].translation[1] == -20.0F);
    assert(lady_variants[0].part_attachments[1].translation[2] == -17.0F);
    assert(lady_variants[1].texture_overrides[2].model_slot == 20U);
    assert(lady_variants[1].texture_overrides[2].texture_slot == 33U);

    auto lady_first = assembly::assemble_pac(*lady, &report, "em034.pac", 0U);
    assert(lady_first != nullptr);
    assert(report.models == 7U);
    assert(lady_first->composite_parts.size() == 7U);
    assert(lady_first->lady_component_bindings.size() == 5U);
    assert(lady_first->lady_dynamic_visuals.size() == 3U);
    assert(lady_first->lady_dynamic_visuals[0].actor == 2U);
    assert(lady_first->lady_dynamic_visuals[0].model_slot == 25U);
    assert(lady_first->lady_dynamic_visuals[1].actor == 3U);
    assert(lady_first->lady_dynamic_visuals[1].model_slot == 26U);
    assert(lady_first->lady_dynamic_visuals[2].actor == 3U);
    assert(lady_first->lady_dynamic_visuals[2].model_slot == 30U);
    assert(!lady_first->lady_dynamic_visuals[0].active);
    assert(!lady_first->lady_dynamic_visuals[1].active);
    assert(!lady_first->lady_dynamic_visuals[2].active);
    assert(lady_first->composite_parts[0].name.find("slot_0001.mod") != std::string::npos);
    assert(lady_first->composite_parts[1].name.find("slot_0017.mod") != std::string::npos);
    for (std::size_t i = 0U; i < 5U; ++i) {
        const auto& binding = lady_first->lady_component_bindings[i];
        assert(binding.component == i);
        assert(binding.model_slot == expected_slots[i]);
        assert(binding.preset == dmcresource::motion::LadyPlacementPreset::BodyStowed);
        assert(binding.control_domain ==
               dmcresource::motion::LadyControlDomain::BodyConstraint);
        assert(lady_first->composite_parts[binding.part].placement.attachment_selector ==
               expected_stowed_joints[i]);
    }

    // Dynamic resources are still present in the PAC child graph but absent
    // from the persistent composite.
    for (const auto& part : lady_first->composite_parts) {
        assert(part.name.find("slot_0025.mod") == std::string::npos);
        assert(part.name.find("slot_0026.mod") == std::string::npos);
        assert(part.name.find("slot_0030.mod") == std::string::npos);
    }

    // Exact preset switching works for body-joint components and for
    // component3's RuntimeBodyRootScaled parent. The latter materializes on
    // body root node0 and never uses the raw serialized node13 as its parent.
    assert(dmcresource::motion::set_lady_component_preset(
        lady_first.get(), lady_first->lady_component_bindings[0],
        dmcresource::motion::LadyPlacementPreset::ActiveDeployed));
    assert(lady_first->composite_parts[
        lady_first->lady_component_bindings[0].part].placement.attachment_selector == 9U);
    assert(dmcresource::motion::set_lady_component_preset(
        lady_first.get(), lady_first->lady_component_bindings[3],
        dmcresource::motion::LadyPlacementPreset::ActiveDeployed));
    assert(lady_first->lady_component_bindings[3].preset ==
           dmcresource::motion::LadyPlacementPreset::ActiveDeployed);
    assert(lady_first->composite_parts[
        lady_first->lady_component_bindings[3].part].placement.attachment_selector == 0U);
    assert(lady_first->lady_component_bindings[3].runtime_uniform_scale == 1.0F);

    // State-entry and signal bridge regressions from the canonical CEm034
    // dispatcher. These specifically guard the old single-joint/single-state
    // model and the corrected action50 channel trace.
    {
        const auto entry = dmcresource::motion::apply_lady_state_entry(
            lady_first.get(), 0x7FU);
        assert(entry.recognized && entry.fully_materialized);
        assert(lady_first->lady_component_bindings[1].preset ==
               dmcresource::motion::LadyPlacementPreset::ActiveDeployed);
        assert(lady_first->lady_component_bindings[2].preset ==
               dmcresource::motion::LadyPlacementPreset::ActiveDeployed);
        assert(lady_first->composite_parts[
            lady_first->lady_component_bindings[1].part].placement.attachment_selector == 9U);
        assert(lady_first->composite_parts[
            lady_first->lady_component_bindings[2].part].placement.attachment_selector == 13U);
    }
    {
        const auto entry = dmcresource::motion::apply_lady_state_entry(
            lady_first.get(), 0x81U);
        assert(entry.recognized && entry.fully_materialized);
        assert(lady_first->lady_component_bindings[3].preset ==
               dmcresource::motion::LadyPlacementPreset::ActiveDeployed);
        assert(lady_first->lady_component_bindings[3].runtime_uniform_scale == 1.0F);

        const auto enlarge = dmcresource::motion::apply_lady_signal(
            lady_first.get(), 0x81U, 1U, 1U, 1U);
        assert(enlarge.recognized && enlarge.fully_materialized);
        assert(lady_first->lady_component_bindings[3].runtime_uniform_scale == 1.5F);
        assert(lady_first->composite_parts[
            lady_first->lady_component_bindings[3].part].placement.attachment_selector == 0U);

        const auto normalize = dmcresource::motion::apply_lady_signal(
            lady_first.get(), 0x81U, 1U, 1U, 0U);
        assert(normalize.recognized && normalize.fully_materialized);
        assert(lady_first->lady_component_bindings[3].runtime_uniform_scale == 1.0F);

        const auto restore = dmcresource::motion::apply_lady_signal(
            lady_first.get(), 0x81U, 1U, 2U, 1U);
        assert(restore.recognized && restore.fully_materialized);
        assert(lady_first->lady_component_bindings[3].preset ==
               dmcresource::motion::LadyPlacementPreset::BodyStowed);
    }
    {
        const auto entry = dmcresource::motion::apply_lady_state_entry(
            lady_first.get(), 0x85U);
        assert(entry.recognized);
        const auto deploy = dmcresource::motion::apply_lady_signal(
            lady_first.get(), 0x85U, 1U, 1U, 1U);
        assert(deploy.recognized);
        assert(lady_first->lady_component_bindings[4].preset ==
               dmcresource::motion::LadyPlacementPreset::ActiveDeployed);
        const auto flag_off = dmcresource::motion::apply_lady_signal(
            lady_first.get(), 0x85U, 1U, 0U, 2U);
        assert(flag_off.recognized && flag_off.runtime_side_effect);
        // frame39/ch0=2 is NOT the placement restore.
        assert(lady_first->lady_component_bindings[4].preset ==
               dmcresource::motion::LadyPlacementPreset::ActiveDeployed);
        const auto stow = dmcresource::motion::apply_lady_signal(
            lady_first.get(), 0x85U, 1U, 1U, 2U);
        assert(stow.recognized);
        assert(lady_first->lady_component_bindings[4].preset ==
               dmcresource::motion::LadyPlacementPreset::BodyStowed);
    }
    {
        // Full entry-dispatch regression. CEm034 state numbers are integer
        // values; state 59 decimal == 0x3B and is asymmetric:
        // lane0 bank0/action1, lane1 bank3/action13.
        const auto mapped =
            dmcresource::motion::lady_state_for_body_script_action(3U, 13U);
        assert(mapped.has_value());
        assert(mapped->state == 0x3BU);
        assert(mapped->lane_mask == 0x2U);
        const auto starts =
            dmcresource::motion::lady_body_state_scripts(0x3BU);
        assert(starts.lanes[0].valid);
        assert(starts.lanes[0].bank == 0U);
        assert(starts.lanes[0].action == 1U);
        assert(starts.lanes[1].valid);
        assert(starts.lanes[1].bank == 3U);
        assert(starts.lanes[1].action == 13U);

        // State 0x59 is 89 decimal and belongs to bank4/action6 on both lanes.
        const auto state89 =
            dmcresource::motion::lady_body_state_scripts(0x59U);
        assert(state89.lanes[0].valid && state89.lanes[1].valid);
        assert(state89.lanes[0].bank == 4U && state89.lanes[0].action == 6U);
        assert(state89.lanes[1].bank == 4U && state89.lanes[1].action == 6U);

        const auto state85 =
            dmcresource::motion::lady_body_state_scripts(0x85U);
        assert(state85.lanes[0].valid && state85.lanes[1].valid);
        assert(state85.lanes[0].bank == 4U && state85.lanes[0].action == 50U);
        assert(state85.lanes[1].bank == 4U && state85.lanes[1].action == 50U);
    }
    {
        const auto entry = dmcresource::motion::apply_lady_state_entry(
            lady_first.get(), 0x59U);
        assert(entry.recognized);
        assert(lady_first->lady_component_bindings[0].preset ==
               dmcresource::motion::LadyPlacementPreset::ActiveDeployed);
        const auto delayed = dmcresource::motion::apply_lady_signal(
            lady_first.get(), 0x59U, 0U, 0U, 1U);
        assert(delayed.recognized);
        assert(lady_first->lady_component_bindings[0].preset ==
               dmcresource::motion::LadyPlacementPreset::BodyStowed);
        assert(lady_first->lady_component_bindings[0].control_domain ==
               dmcresource::motion::LadyControlDomain::IndependentMotionScript);
    }
    {
        const auto rocket = dmcresource::motion::apply_lady_signal(
            lady_first.get(), 0x56U, 1U, 0U, 1U);
        assert(rocket.recognized && rocket.dynamic_actor == 2);
        const auto grapple = dmcresource::motion::apply_lady_state_entry(
            lady_first.get(), 0x5EU);
        assert(grapple.recognized && grapple.dynamic_actor == 3);
        const auto shl04 = dmcresource::motion::apply_lady_signal(
            lady_first.get(), 0x8FU, 1U, 0U, 1U);
        assert(shl04.recognized && shl04.dynamic_actor == 4);
    }

    auto lady_second = assembly::assemble_pac(*lady, &report, "em034.pac", 1U);
    assert(lady_second != nullptr);
    assert(report.models == 7U);
    assert(lady_second->composite_parts.size() == 7U);
    assert(lady_second->lady_component_bindings.size() == 5U);
    assert(lady_second->lady_dynamic_visuals.size() == 3U);
    assert(!lady_second->lady_dynamic_visuals[0].active);
    assert(!lady_second->lady_dynamic_visuals[1].active);
    assert(!lady_second->lady_dynamic_visuals[2].active);
    const auto has_lady_part = [](const dmcresource::Session& session,
                                  std::string_view token) {
        return std::any_of(
            session.composite_parts.begin(), session.composite_parts.end(),
            [token](const auto& part) {
                return part.name.find(token) != std::string::npos;
            });
    };
    assert(has_lady_part(*lady_second, "slot_0032.mod"));
    assert(has_lady_part(*lady_second, "slot_0034.mod"));

    // An archive without MOD is browsable but has nothing to assemble.
    const auto motions_only = make_pac({mot});
    auto no_model = dmcresource::open_session("mot.pac", motions_only.data(), motions_only.size());
    assert(no_model != nullptr);
    assert(assembly::assemble_pac(*no_model) == nullptr);
    return 0;
}
