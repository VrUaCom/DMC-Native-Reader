#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "dmcresource/composite_model.h"
#include "dmcresource/motion/cloth_chain.h"
#include "dmcresource/render_scene.h"

namespace dmcresource {
struct Session;
}

namespace dmcresource::motion {

// C++23 port of the IPlayer attachment contract. Reverse authority:
// dmc-rengine-cpp include/dmc_rengine/profiles/dmc3/player_attachment_contract.hpp
// with docs/research/dmc3-player-coat-attachment-2026-09-23.md and
// dmc3-player-weapon-attachment-2026-09-23.md.
//  * coat: PAC slot 12, texture slot 0, root = identity x bodyJoint[3]
//    (CPlVergil 0x140225A16, CPlDante 0x1402120A7, CPlNewVergil 0x1402204E9);
//  * weapons: root = local(T, R) x player.joint(j) (0x1401FD8F0, 0x140231505).
inline constexpr std::uint32_t kPlayerCoatHostJoint = 3U;
inline constexpr std::uint32_t kPlayerBodySlot = 1U;
inline constexpr std::uint32_t kPlayerCoatSlot = 12U;
inline constexpr std::uint32_t kPlayerTextureSlot = 0U;
// IPlayer coat chain parameters (";pl000_02.clt" in the sample pl000.pac).
inline constexpr std::uint32_t kPlayerCoatClothSlot = 13U;

// Chain (.clt) slot driving a model slot of an enemy archive: CEm028 init
// 0x140130480 caches slots 7/8 for hair/dress; CEm000..CEm003 inits pair each
// cloth model with the slot before it (0x140097B40 .. 0x1400A6BD0).
struct EnemyClothSource final {
    std::string_view pac_stem;
    std::uint32_t model_slot;
    std::uint32_t clt_slot;
};

inline constexpr std::array<EnemyClothSource, 8> kEnemyClothSources{{
    {"em028", 4U, 7U},
    {"em028", 5U, 8U},
    {"em000", 3U, 2U},
    {"em000", 7U, 6U},
    {"em000", 10U, 9U},
    {"em000", 12U, 11U},
    {"em000", 15U, 14U},
    {"em000", 17U, 16U},
}};

// Texture scroll (.tsc) slot and the model slots its CDrawUV objects drive:
// CEm028 init caches slot 13 (0x14013065F) and hands it to the CDrawUV at
// this+0x2D00 for the dress (slot 5, 0x1401307FD) and at this+0x2D38 for the
// sleeves (slot 6, 0x14013089E).
struct TscSource final {
    std::string_view pac_stem;
    std::uint32_t tsc_slot;
    std::array<std::uint32_t, 2> model_slots;
    std::uint32_t model_count;
};

inline constexpr std::array<TscSource, 1> kTscSources{{
    {"em028", 13U, {5U, 6U}, 2U},
}};

// The .tsc slot driving `model_slot` of archive `archive_name`, if any.
[[nodiscard]] std::optional<std::uint32_t> tsc_slot_for(std::string_view archive_name,
                                                        std::uint32_t model_slot) noexcept;

// Match "<stem>.pac" (any directory, any case) and a model slot to its .clt slot.
[[nodiscard]] std::optional<std::uint32_t> enemy_cloth_slot(std::string_view archive_name,
                                                            std::uint32_t model_slot) noexcept;

struct WeaponAttachRecord final {
    std::string_view class_name;
    std::string_view pac_stem;
    std::uint32_t joint;
    std::array<float, 3> translation;
    std::array<float, 3> rotation_xyz_radians;
};

inline constexpr std::array<WeaponAttachRecord, 8> kWeaponState0Records{{
    {"CPlWpSword", "plwp_sword", 3U, {-14.5F, 32.0F, -14.0F},
     {-1.6580626964569092F, 0.0F, 3.4033920764923096F}},
    {"CPlWp2Sword", "plwp_2sword", 3U, {16.0F, -43.0F, -15.0F},
     {-1.6057028770446777F, 0.0F, 0.2617993950843811F}},
    {"CPlWpGuitar", "plwp_guitar", 3U, {-30.0F, -80.0F, -23.0F},
     {-1.5009831190109253F, -0.11344639956951141F, -0.5235987901687622F}},
    {"CPlWpLaser", "plwp_laser", 8U, {0.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 0.0F}},
    {"CPlWpFoeceEdge", "plwp_forceedge", 3U, {-14.5F, 32.0F, -14.0F},
     {-1.6580626964569092F, 0.0F, 3.4033920764923096F}},
    {"CPlWpNeroSword", "plwp_nerosword", 3U, {-14.5F, 32.0F, -14.0F},
     {-1.6580626964569092F, 0.0F, 3.4033920764923096F}},
    {"CPlWpVergilSword", "plwp_vergilsword", 13U, {19.0F, -0.5F, 11.0F},
     {0.0F, 3.839724063873291F, 0.0F}},
    {"CPlWpNewVergilSword", "plwp_newvergilsword", 13U, {19.0F, -0.5F, 11.0F},
     {0.0F, 3.839724063873291F, 0.0F}},
}};

// Enemy node constraints. Reverse authority: dmc-rengine-cpp
// docs/research/dmc3-em028-nevan-assembly-2026-09-23.md and
// profiles/dmc3/enemy_node_constraint_contract.hpp. CEm028 (Nevan) loads
// body slot 1 and parts 4, 5, 6 with texture slot 0 (0x140130480); its init
// links the listed part nodes to body joints (mode 1, identity offset), the
// remaining part nodes are cloth/bat chains (0x1402C9DC0) not simulated here.
struct EnemyPartConstraints final {
    std::string_view pac_stem;
    std::uint32_t body_slot;
    std::uint32_t part_slot;
    std::span<const CompositeNodeConstraint> constraints;
};

inline constexpr std::array<CompositeNodeConstraint, 3> kEm028Slot4{{{0U, 3U}, {1U, 4U}, {2U, 5U}}};
inline constexpr std::array<CompositeNodeConstraint, 4> kEm028Slot5{
    {{0U, 1U}, {1U, 14U}, {2U, 2U}, {3U, 3U}}};
inline constexpr std::array<CompositeNodeConstraint, 5> kEm028Slot6{
    {{0U, 14U}, {1U, 7U}, {6U, 11U}, {2U, 8U}, {7U, 12U}}};

inline constexpr std::array<EnemyPartConstraints, 3> kEnemyPartConstraints{{
    {"em028", 1U, 4U, kEm028Slot4},
    {"em028", 1U, 5U, kEm028Slot5},
    {"em028", 1U, 6U, kEm028Slot6},
}};

// Match "em028.pac" (any directory, any case) and a top-level part slot.
[[nodiscard]] std::optional<EnemyPartConstraints> enemy_constraints_for(
    std::string_view archive_name, std::uint32_t part_slot) noexcept;

// Hang `child_part` from `host_part` through per-node constraints.
[[nodiscard]] bool attach_part_nodes(Session* session,
                                     std::size_t host_part,
                                     std::size_t child_part,
                                     std::span<const CompositeNodeConstraint> constraints) noexcept;

// Two-part weapons. CPlWp2Sword (Agni & Rudra) is one MOD whose node 2
// carries Agni and node 1 Rudra; 0x1401FDA80 builds part 0 from record
// +0x03/+0x10/+0x20 and part 1 from +0x31/+0x40/+0x50, and pose 0x140227CF0
// sets node2 = local(part0) x joint(+0x114), node1 = local(part1) x
// joint(+0x115), node0 = player world. State-0 record 0x14058C1A0.
struct WeaponSecondPart final {
    std::string_view class_name;
    std::uint32_t first_node;   // node driven by the record's first part
    std::uint32_t second_node;  // node driven by the second part
    std::uint32_t joint;
    std::array<float, 3> translation;
    std::array<float, 3> rotation_xyz_radians;
};

inline constexpr std::array<WeaponSecondPart, 1> kWeaponSecondParts{{
    {"CPlWp2Sword", 2U, 1U, 3U, {-13.0F, 32.0F, -14.0F},
     {-1.6580626964569092F, 0.0F, 3.4033920764923096F}},
}};

[[nodiscard]] std::optional<WeaponSecondPart> weapon_second_part(
    std::string_view class_name) noexcept;

// local(T, R) built like the MOD rest local (0x140330450 + 0x140031200).
[[nodiscard]] Matrix4 attach_local_matrix(const std::array<float, 3>& translation,
                                          const std::array<float, 3>& rotation_xyz_radians) noexcept;

// Enemy families sharing one archive. em000.pac feeds five classes
// (CEm000-CEm004); each init (0x140097B40, 0x14009CF70, 0x1400A1D80,
// 0x1400A6BD0, 0x1400A85E0) reads its own body slot, cloth slots (with their
// .clt text slot) and weapon slot; the shared update (0x1401C6FB0 family)
// roots cloth models at body joints [this+0x3254]/[this+0x3258] and the
// weapon at offset(this+0x28E0) x body joint 9. The weapon slot shown is the
// one loaded for variant 0-1 ([this+0x670]); 2-3 load the other weapon slot.
// Offsets are built by 0x1403304A0: Rz x Ry x Rx, then translation.
struct EnemyClothPart final {
    std::uint32_t slot;
    std::uint32_t host_joint;
};

struct EnemyVariant final {
    std::string_view pac_stem;
    std::string_view class_name;
    std::uint32_t body_slot;
    std::array<EnemyClothPart, 2> cloth;
    std::uint32_t cloth_count;
    std::uint32_t weapon_slot;      // [this+0x670] in 0..1
    std::uint32_t weapon_slot_alt;  // [this+0x670] in 2..3 (same slot: no choice)
    bool cloth_only_first_variant;  // CEm000: cloth drawn for 0..1 only (0x140097980)
    std::uint32_t weapon_joint;
    std::array<float, 3> weapon_translation;
    std::array<float, 3> weapon_rotation_zyx;
};

inline constexpr std::array<float, 3> kEm000WeaponT{-15.0F, -61.39939880371094F,
                                                   -18.93269920349121F};
inline constexpr std::array<float, 3> kEm000WeaponR{0.20725786685943604F, 0.0F, 0.0F};

inline constexpr std::array<EnemyVariant, 5> kEm000Variants{{
    {"em000", "CEm000", 1U, {{{3U, 14U}, {0U, 0U}}}, 1U, 26U, 29U, true, 9U, kEm000WeaponT,
     kEm000WeaponR},
    {"em000", "CEm001", 5U, {{{7U, 14U}, {0U, 0U}}}, 1U, 28U, 31U, false, 9U, kEm000WeaponT,
     kEm000WeaponR},
    {"em000", "CEm002", 8U, {{{10U, 8U}, {12U, 12U}}}, 2U, 26U, 29U, false, 9U, kEm000WeaponT,
     kEm000WeaponR},
    {"em000", "CEm003", 13U, {{{15U, 14U}, {17U, 14U}}}, 2U, 27U, 30U, false, 9U, kEm000WeaponT,
     kEm000WeaponR},
    {"em000", "CEm004", 18U, {{{0U, 0U}, {0U, 0U}}}, 0U, 34U, 34U, false, 9U, {2.0F, 20.0F, -72.0F},
     {-0.03490658476948738F, 0.10471975803375244F, 1.6580626964569092F}},
}};

// Variants for an archive name ("em000.pac", any directory, any case).
[[nodiscard]] std::span<const EnemyVariant> enemy_variants_for(
    std::string_view archive_name) noexcept;

// One selectable position of an archive with several in-game looks: an
// enemy class and its weapon variant (em000.pac), or a model-object state
// (em028.pac: the dress strip, objects 2-3 of slot 5, is drawn only while
// bats are out -- 0x14012F790 sets or clears object bit 0, which the MOD draw
// loops 0x140303460 / 0x140303DE0 require).
struct ArchiveVariant final {
    std::string label;
    const EnemyVariant* enemy{};  // em000 family
    bool alternate_weapon{};      // [this+0x670] in 2..3
    std::uint32_t hide_slot{};    // model slot whose objects are hidden
    std::array<std::uint32_t, 2> hide_objects{};
    std::uint32_t hide_count{};
};

[[nodiscard]] std::vector<ArchiveVariant> archive_variants(std::string_view archive_name);

// Translation plus Rz x Ry x Rx (0x1403304A0 order).
[[nodiscard]] Matrix4 attach_local_matrix_zyx(const std::array<float, 3>& translation,
                                              const std::array<float, 3>& rotation_xyz_radians) noexcept;

// Weapon motion banks. The weapon factory 0x1401DED20 creates the melee
// classes from ids 0-4, 11, 12, 14 and the gun classes from ids 5-10, 13;
// Dante's loader 0x1401DF6BE loads motion\\pl000\\pl000_00_N.pac with
// N = byte 0x14058ABC8[id * 4] through 0x1401B90B0 (path table 0x1405B0F30).
struct WeaponMotionBank final {
    std::uint8_t weapon_id;
    std::string_view class_name;
    std::string_view weapon_name;  // common name for the class
    std::uint8_t file_index;       // pl000_00_<file_index>.pac
};

inline constexpr std::array<WeaponMotionBank, 15> kDanteWeaponMotionBanks{{
    {0U, "CPlWpSword", "Rebellion", 3U},
    {1U, "CPlWpNunchaku", "Cerberus", 4U},
    {2U, "CPlWp2Sword", "Agni & Rudra", 5U},
    {3U, "CPlWpGuitar", "Nevan", 6U},
    {4U, "CPlWpFight", "Beowulf", 7U},
    {5U, "CPlWpGun", "Ebony & Ivory", 8U},
    {6U, "CPlWpShotGun", "Shotgun", 9U},
    {7U, "CPlWpLaser", "Artemis", 10U},
    {8U, "CPlWpRifle", "Spiral", 11U},
    {9U, "CPlWpLadyGun", "Kalina Ann", 12U},
    {10U, "CPlWpLadyGun", "Kalina Ann (id 10)", 27U},
    {11U, "CPlWpNewVergilSword", "Yamato (CPlWpNewVergilSword)", 28U},
    {12U, "CPlWpFight", "Beowulf (id 12)", 29U},
    {13U, "CPlWpFoeceEdge", "Force Edge", 30U},
    {14U, "CPlWpVergilSword", "Yamato (CPlWpVergilSword)", 31U},
}};

// Match "pl000_00_<N>.pac" (any directory, any case) to its weapon bank.
[[nodiscard]] std::optional<WeaponMotionBank> weapon_motion_bank(
    std::string_view archive_name) noexcept;

// Match a PAC file name (any directory, any case, ".pac") to a weapon record.
[[nodiscard]] std::optional<WeaponAttachRecord> weapon_record_for_archive(
    std::string_view archive_name) noexcept;

// local(T, R) built exactly like the MOD rest local (0x140330450 + 0x140031200).
[[nodiscard]] Matrix4 weapon_offset_matrix(const WeaponAttachRecord& record) noexcept;

// Hang `child_part`'s skeleton from `host_joint` of `host_part` and pose it
// immediately from the host joint's current world. Read-only: only the
// derived composite projection changes.
[[nodiscard]] bool attach_part_skeleton(Session* session,
                                        std::size_t host_part,
                                        std::size_t child_part,
                                        std::uint32_t host_joint,
                                        bool root_local_identity,
                                        const Matrix4& offset = Matrix4{}) noexcept;

// Re-pose every HostJointSkeleton part from its host joint's current world.
// Called after the host moved (MOT frame) and after attachment.
[[nodiscard]] bool apply_part_attachments(Session* session) noexcept;
// Same, advancing every attached cloth by `cloth_steps` solver frames (dt 1).
[[nodiscard]] bool apply_part_attachments(Session* session, std::uint32_t cloth_steps) noexcept;

// Forget the simulated chain state (next pose restarts from the rest pose).
void reset_part_cloth(Session* session) noexcept;

// Simulate the nodes listed by `clt_text` (first cloth block) on an attached
// part and settle it for `settle_steps` frames from its current pose.
// Returns the number of simulated nodes (0 = not a cloth file / no match).
[[nodiscard]] std::size_t attach_part_cloth(Session* session,
                                            std::size_t part,
                                            std::string_view clt_text,
                                            std::uint32_t settle_steps = 60U,
                                            std::span<const ClothCapsule> capsules = {}) noexcept;

[[nodiscard]] bool is_attached_part(const Session* session, std::size_t part) noexcept;

}  // namespace dmcresource::motion
