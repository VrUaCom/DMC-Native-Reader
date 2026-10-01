#pragma once

#include <cstdint>
#include <span>

#include "dmcresource/motion/effect_runtime.h"

namespace dmcresource {
struct Session;
}

// Enemy effects driven by the AI command layer of dmc3.exe (em000 family:
// CEm000..CEm004, shared command class CComEm000, vtable 0x1404C6A18).
// A command plays a script action through the enemy interface slot +0x20
// (command n = bank 0 action n, table 0x1405A3300) and, once the motion frame
// (slot +0x40) passes a constant, sends an event code through slot +0x80 to
// the handler 0x1401C3130. Code 3 spawns E42 and V42, scaled x2, following
// script object 1 (the weapon) by position only (effect +0xC8 / +0xD4 = 1).
// The (action, frame) pairs come from emulated runs of every command of
// CComEm000; reverse note: dmc-rengine-cpp
// docs/research/dmc3-em000-attack-effects-2026-10-01.md.
namespace dmcresource::motion {

struct EnemyAttackEvent final {
    std::uint8_t bank{};
    std::uint8_t action{};
    float frame{};      // motion frame the command waits for (frame >= value)
    std::uint8_t code{};  // event code for 0x1401C3130
};

inline constexpr std::uint8_t kEm000WeaponActor = 0x20U;
inline constexpr std::uint32_t kEm000ScriptSlot = 38U;
inline constexpr std::uint32_t kEm000EffectSlot = 41U;

[[nodiscard]] std::span<const EnemyAttackEvent> em000_attack_events() noexcept;
[[nodiscard]] std::span<const EffectBinding> em000_effect_bindings() noexcept;
[[nodiscard]] bool em000_profile_matches(const dmcresource::Session* session) noexcept;

// Events of `action` whose frame lies in (from, to]; `from` < 0 includes frame 0.
template <class Fn>
void for_each_em000_event(std::size_t bank, std::size_t action, float from, float to, Fn&& fn) {
    const auto events = em000_attack_events();
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
