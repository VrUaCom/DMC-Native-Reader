#include "dmcresource/motion/enemy_effects.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <limits>
#include <optional>
#include <string_view>

#include "dmcresource/motion/motion_player.h"
#include "dmcresource/resource_session.h"

namespace dmcresource::motion {
namespace {

// Emulated CComEm000 commands (update / start pairs of vtable 0x1404C6A18)
// whose frame gate falls inside the played action's MOT (em000.pac slot 35):
//   slot 193 / 0x14006B190: action 62 (MOT 55, 109 frames) code 3 at 40, 50
//   slot 235 / 0x14006B900: action 66 (MOT 60, 124) code 3 at 30
//   slots 189, 191 / 0x14006AE00, 0x14006AA70: action 68 (MOT 46, 60) at 40, 50
//   slot 229 / 0x140066F10: action 82 (MOT 62, 124) at 76
//   slot 231 / 0x140067120: action 84 (MOT 64, 300) at 180
//   slot 237 / 0x14006B520: action 85 (MOT 65, 232) at 100, then action 87
//                           (MOT 47, 142) at 30
// Gates past the MOT end (slot 207: action 68 at 100 / 110; slot 233: action
// 87 at 170) are left out: they fire only if the frame runs past the end.
constexpr std::array<EnemyAttackEvent, 9> kEm000AttackEvents{{
    {0U, 62U, 40.0F, 3U}, {0U, 62U, 50.0F, 3U},
    {0U, 66U, 30.0F, 3U},
    {0U, 68U, 40.0F, 3U}, {0U, 68U, 50.0F, 3U},
    {0U, 82U, 76.0F, 3U},
    {0U, 84U, 180.0F, 3U},
    {0U, 85U, 100.0F, 3U},
    {0U, 87U, 30.0F, 3U},
}};

[[nodiscard]] bool has_stem(std::string_view name, std::string_view stem) noexcept {
    if (name.size() < stem.size()) return false;
    for (std::size_t start = 0U; start + stem.size() <= name.size(); ++start) {
        bool match = true;
        for (std::size_t i = 0U; i < stem.size() && match; ++i) {
            match = std::tolower(static_cast<unsigned char>(name[start + i])) == stem[i];
        }
        if (match) return true;
    }
    return false;
}

// Position-only follow of the weapon (mode 1 of 0x1402E7FF0) with the x2
// scale the handler applies (0x1403304F0 before 0x1402E7AB0).
[[nodiscard]] std::optional<Matrix4> weapon_effect_world(const Session& session) noexcept {
    if (!session.enemy_weapon_part.has_value()) return std::nullopt;
    const auto index = *session.enemy_weapon_part;
    if (index >= session.composite_parts.size() || session.composite_parts[index].scene.nodes.empty()) {
        return std::nullopt;
    }
    std::size_t begin = 0U;
    for (std::size_t i = 0U; i < index; ++i) begin += session.composite_parts[i].scene.nodes.size();
    if (begin >= session.scene.nodes.size()) return std::nullopt;
    const auto& node = session.scene.nodes[begin].world;
    Matrix4 out;
    out.values = {2.0F, 0.0F, 0.0F, 0.0F, 0.0F, 2.0F, 0.0F, 0.0F, 0.0F, 0.0F, 2.0F, 0.0F,
                  node.values[12], node.values[13], node.values[14], 1.0F};
    return out;
}

}  // namespace

std::span<const EnemyAttackEvent> em000_attack_events() noexcept {
    return kEm000AttackEvents;
}

std::span<const EffectBinding> em000_effect_bindings() noexcept {
    // Handler 0x1401C3130 case 3: 0x1402E7AB0(1, 42, scaled matrix, 0, 0x10)
    // and 0x1402E7AB0(3, 42, ...); both get +0xC8 = object 1 (+0x110) and
    // +0xD4 = 1. They end through their own records (E42 lives 23 ticks).
    static constexpr std::array<EffectBinding, 2> kBindings{{
        {kEm000WeaponActor, 'E', 42U, kEm000EffectSlot, RuntimeEffectParent::DynamicActor,
         0xFFFFU, 0xFFU, 0xFFU, 0xFFU, EvidenceStatus::EXE_AND_CORPUS_CONFIRMED,
         EffectLifetimeRule::EffectCallback, EvidenceStatus::EXE_CONFIRMED,
         std::span<const EffectChildRef>{}, 0U},
        {kEm000WeaponActor, 'V', 42U, kEm000EffectSlot, RuntimeEffectParent::DynamicActor,
         0xFFFFU, 0xFFU, 0xFFU, 0xFFU, EvidenceStatus::EXE_AND_CORPUS_CONFIRMED,
         EffectLifetimeRule::EffectCallback, EvidenceStatus::EXE_CONFIRMED,
         std::span<const EffectChildRef>{}, 0U},
    }};
    return kBindings;
}

bool em000_profile_matches(const Session* session) noexcept {
    if (session == nullptr || !has_stem(session->archive_name, "em000")) return false;
    if (std::find(session->effect_bank_slots.begin(), session->effect_bank_slots.end(),
                  kEm000EffectSlot) == session->effect_bank_slots.end()) {
        return false;
    }
    // CComEm000 drives CEm000..CEm004; CEm005 has its own command class.
    const std::string_view cls = session->enemy_class;
    return session->enemy_weapon_part.has_value() &&
           (cls == "CEm000" || cls == "CEm001" || cls == "CEm002" || cls == "CEm003" ||
            cls == "CEm004");
}

void em000_bridge_step(Session* session, float frame) noexcept {
    if (session == nullptr || session->effect_runtime == nullptr || !std::isfinite(frame)) return;
    try {
        const auto action = current_script_action(session);
        if (!action.has_value() || action->script_slot != kEm000ScriptSlot) return;
        const auto world = weapon_effect_world(*session);
        if (!world.has_value()) return;
        auto& runtime = *session->effect_runtime;
        const auto spawned = [&runtime](std::size_t index) -> const DynamicActorEvent* {
            for (const auto& e : runtime.actor_events()) {
                if (e.kind == DynamicActorEventKind::Spawn && e.actor == kEm000WeaponActor &&
                    e.actor_state == index) {
                    return &e;
                }
            }
            return nullptr;
        };
        for_each_em000_event(action->bank, action->action, -1.0F, frame,
                             [&](std::size_t index, const EnemyAttackEvent& event) {
            const auto* spawn = spawned(index);
            DynamicActorEvent out;
            out.actor = kEm000WeaponActor;
            out.actor_state = static_cast<std::uint16_t>(index);
            out.world = *world;
            out.world_authoritative = true;
            out.evidence = EvidenceStatus::EXE_AND_CORPUS_CONFIRMED;
            if (spawn == nullptr) {
                out.kind = DynamicActorEventKind::Spawn;
                out.script_frame = event.frame;
                runtime.apply_actor_event(out);
                return;
            }
            // Follow the weapon for as long as the spawned records can live.
            if (frame - event.frame > 90.0F) return;
            out.kind = DynamicActorEventKind::Update;
            out.actor_instance = spawn->actor_instance;
            out.script_frame = frame;
            runtime.apply_actor_event(out);
        });
    } catch (...) {
    }
}

void em000_bridge_reset(Session* session) noexcept {
    (void)session;  // the runtime reset clears the actor events
}

}  // namespace dmcresource::motion
