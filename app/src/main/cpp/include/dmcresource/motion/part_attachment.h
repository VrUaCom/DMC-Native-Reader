#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

#include "dmcresource/composite_model.h"
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

[[nodiscard]] bool is_attached_part(const Session* session, std::size_t part) noexcept;

}  // namespace dmcresource::motion
