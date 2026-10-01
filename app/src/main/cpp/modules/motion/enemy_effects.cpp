#include "dmcresource/motion/enemy_effects.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <limits>
#include <optional>
#include <string_view>
#include <vector>

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

// World of body joint `joint` (entry `joint` of obj+0x6D8). Mode 0 copies
// it, mode 1 keeps only its position (0x1402E7FF0); `scale` is the uniform
// scale the handler applies to the spawn matrix (0x1403304F0).
[[nodiscard]] std::optional<Matrix4> body_joint_effect_world(
    const Session& session, std::size_t joint, std::uint8_t mode, float scale) noexcept {
    if (!session.enemy_body_part.has_value()) return std::nullopt;
    const auto index = *session.enemy_body_part;
    if (index >= session.composite_parts.size() ||
        joint >= session.composite_parts[index].scene.nodes.size()) {
        return std::nullopt;
    }
    std::size_t begin = 0U;
    for (std::size_t i = 0U; i < index; ++i) begin += session.composite_parts[i].scene.nodes.size();
    if (begin + joint >= session.scene.nodes.size()) return std::nullopt;
    const auto& node = session.scene.nodes[begin + joint].world;
    Matrix4 out;
    if (mode == 0U) {
        out = node;
        for (std::size_t i = 0U; i < 12U; ++i) out.values[i] *= scale;
        return out;
    }
    out.values = {scale, 0.0F, 0.0F, 0.0F, 0.0F, scale, 0.0F, 0.0F, 0.0F, 0.0F, scale, 0.0F,
                  node.values[12], node.values[13], node.values[14], 1.0F};
    return out;
}

// Death state 0x140095E85 (CEm000; the other classes have copies): on entry
// codes 0x69 and 0xC8, then the timer obj+0x2EFC (+= dt) sends 0xCA + 0xCB at
// 5, 0xC9 at 10, 0xCD at 15, 0xCC at 20 and 0xCE at 25. Spawns per code from
// the emulated handler (no-player path, enemy type != 0x1A).
constexpr std::array<EnemyDeathSpawn, 28> kEm000DeathSpawns{{
    {0.0F, 0x69U, 'V', 132U, 9U, 0U},
    {0.0F, 0xC8U, 'V', 315U, 4U, 0U}, {0.0F, 0xC8U, 'P', 32U, 5U, 1U}, {0.0F, 0xC8U, 'P', 32U, 21U, 1U},
    {5.0F, 0xCAU, 'V', 317U, 7U, 0U}, {5.0F, 0xCAU, 'V', 317U, 8U, 0U}, {5.0F, 0xCAU, 'P', 32U, 7U, 1U},
    {5.0F, 0xCAU, 'P', 32U, 8U, 1U}, {5.0F, 0xCAU, 'P', 32U, 9U, 1U},
    {5.0F, 0xCBU, 'V', 318U, 11U, 0U}, {5.0F, 0xCBU, 'V', 318U, 12U, 0U}, {5.0F, 0xCBU, 'P', 32U, 11U, 1U},
    {5.0F, 0xCBU, 'P', 32U, 12U, 1U}, {5.0F, 0xCBU, 'P', 32U, 13U, 1U},
    {10.0F, 0xC9U, 'V', 316U, 3U, 0U}, {10.0F, 0xC9U, 'P', 32U, 4U, 1U}, {10.0F, 0xC9U, 'P', 32U, 3U, 1U},
    {10.0F, 0xC9U, 'P', 32U, 6U, 1U}, {10.0F, 0xC9U, 'P', 32U, 10U, 1U},
    {15.0F, 0xCDU, 'P', 32U, 2U, 1U},
    {20.0F, 0xCCU, 'V', 319U, 1U, 0U}, {20.0F, 0xCCU, 'P', 32U, 14U, 1U}, {20.0F, 0xCCU, 'P', 32U, 15U, 1U},
    {20.0F, 0xCCU, 'P', 32U, 18U, 1U},
    {25.0F, 0xCEU, 'P', 32U, 16U, 1U}, {25.0F, 0xCEU, 'P', 32U, 19U, 1U},
    {25.0F, 0xCEU, 'P', 32U, 17U, 1U}, {25.0F, 0xCEU, 'P', 32U, 20U, 1U},
}};

[[nodiscard]] constexpr const std::array<EnemyDeathSpawn, 28>& death_table() noexcept {
    return kEm000DeathSpawns;
}

// Spawned records follow their joint for as long as they can live.
constexpr float kFollowTicks = 90.0F;

// Spawn once per (actor, index); later steps update the spawned instance.
// actor_events() holds only the current step, so the bridge keeps the
// instances it created in the session.
void emit_joint_event(Session& session, std::uint8_t actor, std::size_t index,
                      float spawn_frame, float frame, const Matrix4& world) {
    auto& runtime = *session.effect_runtime;
    const auto key = (static_cast<std::uint32_t>(actor) << 16U) | static_cast<std::uint32_t>(index);
    auto& spawned = session.enemy_effect_actors;
    if (session.enemy_effect_generation != runtime.reset_count()) {
        spawned.clear();
        session.enemy_effect_generation = runtime.reset_count();
    }
    const auto it = std::find_if(spawned.begin(), spawned.end(),
                                 [key](const auto& entry) { return entry.first == key; });
    DynamicActorEvent out;
    out.actor = actor;
    out.actor_state = static_cast<std::uint16_t>(index);
    out.world = world;
    out.world_authoritative = true;
    out.spawn_matrix = world;
    out.spawn_matrix_authoritative = true;
    out.evidence = EvidenceStatus::EXE_AND_CORPUS_CONFIRMED;
    if (it == spawned.end()) {
        out.kind = DynamicActorEventKind::Spawn;
        out.script_frame = spawn_frame;
        runtime.apply_actor_event(out);
        spawned.emplace_back(key, runtime.actor_events().back().actor_instance);
        return;
    }
    if (frame - spawn_frame > kFollowTicks) return;
    out.kind = DynamicActorEventKind::Update;
    out.actor_instance = it->second;
    out.script_frame = frame;
    runtime.apply_actor_event(out);
}

constexpr std::array<const char*, 1> kEm000ClassEvents{{"Death"}};

}  // namespace

std::span<const EnemyAttackEvent> em000_attack_events() noexcept {
    return kEm000AttackEvents;
}

std::span<const EnemyDeathSpawn> em000_death_spawns() noexcept { return death_table(); }

std::span<const EffectBinding> em000_effect_bindings() noexcept {
    // Handler 0x1401C3130 case 3: 0x1402E7AB0(1, 42, scaled matrix, 0, 0x10)
    // and 0x1402E7AB0(3, 42, ...); both get +0xC8 = obj+0x6D8[1]->+0x110
    // (body joint 1) and +0xD4 = 1. They end through their own records (E42
    // lives 23 ticks). Death spawns: one binding per schedule entry.
    static const auto bindings = [] {
        std::vector<EffectBinding> out;
        const auto make = [](std::uint8_t actor, char kind, std::uint16_t id, std::uint16_t state) {
            return EffectBinding{actor, kind, id, kEm000EffectSlot, RuntimeEffectParent::DynamicActor,
                                 state, 0xFFU, 0xFFU, 0xFFU, EvidenceStatus::EXE_AND_CORPUS_CONFIRMED,
                                 EffectLifetimeRule::EffectCallback, EvidenceStatus::EXE_CONFIRMED,
                                 std::span<const EffectChildRef>{}, 0U};
        };
        out.push_back(make(kEm000WeaponActor, 'E', 42U, 0xFFFFU));
        out.push_back(make(kEm000WeaponActor, 'V', 42U, 0xFFFFU));
        const auto& death = death_table();
        for (std::size_t i = 0U; i < death.size(); ++i) {
            out.push_back(make(kEm000DeathActor, death[i].kind, death[i].id, static_cast<std::uint16_t>(i)));
        }
        return out;
    }();
    return bindings;
}

bool em000_profile_matches(const Session* session) noexcept {
    if (session == nullptr || !has_stem(session->archive_name, "em000")) return false;
    if (std::find(session->effect_bank_slots.begin(), session->effect_bank_slots.end(),
                  kEm000EffectSlot) == session->effect_bank_slots.end()) {
        return false;
    }
    // CComEm000 drives CEm000..CEm004; CEm005 has its own command class.
    const std::string_view cls = session->enemy_class;
    return session->enemy_body_part.has_value() &&
           (cls == "CEm000" || cls == "CEm001" || cls == "CEm002" || cls == "CEm003" ||
            cls == "CEm004");
}

void em000_bridge_step(Session* session, float frame) noexcept {
    if (session == nullptr || session->effect_runtime == nullptr || !std::isfinite(frame)) return;
    try {
        session->enemy_script_frame = frame;
        const auto action = current_script_action(session);
        if (!action.has_value() || action->script_slot != kEm000ScriptSlot) return;
        if (const auto world = body_joint_effect_world(*session, kEm000AttackJoint, 1U, 2.0F)) {
            for_each_em000_event(action->bank, action->action, -1.0F, frame,
                                 [&](std::size_t index, const EnemyAttackEvent& event) {
                emit_joint_event(*session, kEm000WeaponActor, index, event.frame, frame, *world);
            });
        }
        if (!session->enemy_death_start.has_value()) return;
        const float start = *session->enemy_death_start;
        if (frame < start) {  // the motion wrapped or restarted
            session->enemy_death_start.reset();
            return;
        }
        const auto& death = death_table();
        for (std::size_t i = 0U; i < death.size(); ++i) {
            const float spawn_frame = start + death[i].tick;
            if (frame < spawn_frame) continue;
            const auto world = body_joint_effect_world(*session, death[i].joint, death[i].mode, 1.0F);
            if (!world.has_value()) continue;
            emit_joint_event(*session, kEm000DeathActor, i, spawn_frame, frame, *world);
        }
    } catch (...) {
    }
}

void em000_bridge_reset(Session* session) noexcept {
    // Called with the runtime reset (action start, rewind): forget the
    // spawned instances; a restarted action ends a running death preview.
    if (session == nullptr) return;
    session->enemy_death_start.reset();
    session->enemy_effect_actors.clear();
}

std::span<const char* const> class_event_names(const Session* session) noexcept {
    if (!em000_profile_matches(session)) return {};
    return kEm000ClassEvents;
}

bool trigger_class_event(Session* session, std::size_t index) noexcept {
    if (session == nullptr || index >= class_event_names(session).size()) return false;
    if (session->script_effect_bridge.step != em000_bridge_step) return false;
    const auto action = current_script_action(session);
    if (!action.has_value() || action->script_slot != kEm000ScriptSlot) return false;
    // A new death starts from a clean runtime: earlier death spawns go, the
    // attack effects of the running action are re-emitted by the next step.
    if (session->effect_runtime != nullptr) session->effect_runtime->reset();
    session->enemy_effect_actors.clear();
    session->enemy_death_start = session->enemy_script_frame;
    return true;
}

}  // namespace dmcresource::motion
