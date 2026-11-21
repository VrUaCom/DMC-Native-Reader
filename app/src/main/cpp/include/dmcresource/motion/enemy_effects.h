#pragma once

#include <cstdint>
#include <span>
#include <string_view>

#include "dmcresource/motion/effect_runtime.h"

namespace dmcresource {
struct Session;
}

// Enemy effects driven by the AI command layer of dmc3.exe (em000 family:
// CEm000..CEm004, shared command class CComEm000, vtable 0x1404C6A18).
// A command plays a script action through the enemy interface slot +0x20
// (command n = bank 0 action n, table 0x1405A3300) and, once the motion frame
// (slot +0x40) passes a constant, sends an event code through slot +0x80 to
// the handler 0x1401C3130. Effects the handler attaches go to entry k of the
// joint array obj+0x6D8, which the body model bind 0x14030F850 fills with its
// joints (entry k +0x110 = body joint k world): code 3 spawns E42 and V42,
// scaled x2, following body joint 1 by position only (+0xC8 / +0xD4 = 1).
// Every code, not only 3: tools/research/em000_events.py runs the handler for
// each code and every command of CComEm000 (update slot 2k+1, start 2k+2) in
// an emulator and writes em000_events.inc: what each code spawns and where,
// and which (command, frame, code) each command sends; commands become
// (bank, action) through each class's tables (table 2: interface slot
// +0x28). The death schedule is from the death state 0x140095E85. Reverse
// notes: dmc-rengine-cpp docs/research/dmc3-em000-attack-effects-2026-10-01.md,
// dmc3-effect-triggers-2026-10-01.md; docs/research/dmc3-em000-events-v123.md.
namespace dmcresource::motion {

struct EnemyAttackEvent final {
    std::uint8_t bank{};
    std::uint8_t action{};
    float frame{};      // motion frame the command waits for (frame >= value)
    std::uint8_t code{};  // event code for 0x1401C3130
};

// One spawn of the death schedule: the death state sends `code` once the
// timer obj+0x2EFC reaches `tick` (0, 5, 10, 15, 20, 25); the handler places
// the effect on body joint `joint`, mode 0 (full joint world) or 1 (position
// only, 0x1402E7FF0).
struct EnemyDeathSpawn final {
    float tick{};
    std::uint8_t code{};
    char kind{};
    std::uint16_t id{};
    std::uint8_t joint{};
    std::uint8_t mode{};
};

// Where the event handler puts a spawn: on body joint `joint` (mode 0 its
// world, 1 its position only), at the enemy's position (and yaw), or at the
// position the event came with (the enemy's own for the commands).
enum class EventPlace : std::uint8_t { joint = 0, actor = 1, given = 2 };

// One spawn of the event handler 0x1401C3130 for `code` (enemy type other
// than 0x1A), generated from the emulated handler (em000_events.inc).
struct EnemyEventSpawn final {
    std::uint8_t code{};
    char kind{};
    std::uint16_t id{};
    EventPlace place{};
    std::uint8_t joint{};
    std::uint8_t mode{};
    float scale{1.0F};  // uniform scale of the spawn matrix (code 3: 2)
};

inline constexpr std::uint8_t kEm000WeaponActor = 0x20U;  // the event spawns (state: spawn row)
inline constexpr std::uint8_t kEm000DeathActor = 0x21U;
inline constexpr std::uint8_t kEm000AttackJoint = 1U;
inline constexpr std::uint32_t kEm000ScriptSlot = 38U;
inline constexpr std::uint32_t kEm000EffectSlot = 41U;

// Every spawn the handler makes, by code.
[[nodiscard]] std::span<const EnemyEventSpawn> em000_event_spawns() noexcept;
// The events every CComEm000 command sends while its action plays, as
// (bank, action) of `enemy_class` (CEm000..CEm004: each has its command
// tables); empty for another class.
[[nodiscard]] std::span<const EnemyAttackEvent> em000_command_events(std::string_view enemy_class) noexcept;
// CEm000's.
[[nodiscard]] std::span<const EnemyAttackEvent> em000_attack_events() noexcept;
[[nodiscard]] std::span<const EffectBinding> em000_effect_bindings() noexcept;
[[nodiscard]] bool em000_profile_matches(const dmcresource::Session* session) noexcept;
[[nodiscard]] std::span<const EnemyDeathSpawn> em000_death_spawns() noexcept;

// Class events the viewer can send to the playing enemy. em000 family:
// "Death" = control code 0x3E7, which the damage command (0x1400675C0) sends
// with 0x385 on the killing hit; the death state then runs its schedule on
// top of whatever motion plays. Starts at the current Script Play frame.
[[nodiscard]] std::span<const char* const> class_event_names(const dmcresource::Session* session) noexcept;
[[nodiscard]] bool trigger_class_event(dmcresource::Session* session, std::size_t index) noexcept;

// Events of `action` whose frame lies in (from, to]; `from` < 0 includes frame 0.
template <class Fn>
void for_each_em000_event(std::size_t bank, std::size_t action, float from, float to, Fn&& fn,
                          std::string_view enemy_class = "CEm000") {
    const auto events = em000_command_events(enemy_class);
    for (std::size_t i = 0U; i < events.size(); ++i) {
        const auto& e = events[i];
        if (e.bank != bank || e.action != action) continue;
        if (e.frame > from && e.frame <= to) fn(i, e);
    }
}

// Script Play bridge hooks (ScriptEffectBridge::step / reset).
void em000_bridge_step(dmcresource::Session* session, float frame) noexcept;
void em000_bridge_reset(dmcresource::Session* session) noexcept;

}  // namespace dmcresource::motion
