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

// Generated from dmc3.exe (tools/research/em000_events.py).
struct HandlerRow final {
    std::uint8_t code;
    std::uint8_t type_1a;  // enemy type 0x1A takes another path for a few codes
    char kind;
    std::uint16_t id;
    std::uint8_t place;
    std::uint8_t joint;
    std::uint8_t mode;
    float scale;
};
constexpr HandlerRow kHandlerRows[] = {
#define EM000_HANDLER_ROW(code, type_1a, kind, id, place, joint, mode, scale) \
    {code, type_1a, kind, id, place, joint, mode, scale},
#include "em000_events.inc"
#undef EM000_HANDLER_ROW
};

struct CommandEvent final {
    std::uint8_t table;    // 1: interface slot +0x20, 2: +0x28
    std::uint8_t command;
    float frame;
    std::uint8_t code;
};
constexpr CommandEvent kCommandEvents[] = {
#define EM000_COMMAND_EVENT(table, command, frame, code) {table, command, frame, code},
#include "em000_events.inc"
#undef EM000_COMMAND_EVENT
};

// Each class's command tables: (bank, action) for table 1, then table 2.
inline constexpr std::size_t kTableEntries = 128U;
struct ClassTables final {
    std::string_view name;
    std::array<std::array<std::uint8_t, 2>, 2U * kTableEntries> pairs;
};
constexpr ClassTables kClassTables[] = {
#define EM000_CLASS_TABLES(name, ...) {name, {{__VA_ARGS__}}},
#include "em000_events.inc"
#undef EM000_CLASS_TABLES
};

[[nodiscard]] const std::vector<EnemyEventSpawn>& event_spawns() {
    static const auto spawns = [] {
        std::vector<EnemyEventSpawn> out;
        for (const auto& r : kHandlerRows) {
            if (r.type_1a != 0U) continue;
            out.push_back({r.code, r.kind, r.id, static_cast<EventPlace>(r.place), r.joint, r.mode, r.scale});
        }
        return out;
    }();
    return spawns;
}

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
// 5, 0xC9 at 10, 0xCD at 15, 0xCC at 20 and 0xCE at 25. Each code's spawns are
// the handler's.
struct DeathCode final {
    float tick;
    std::uint8_t code;
};
constexpr std::array<DeathCode, 8> kDeathSchedule{{
    {0.0F, 0x69U}, {0.0F, 0xC8U}, {5.0F, 0xCAU}, {5.0F, 0xCBU},
    {10.0F, 0xC9U}, {15.0F, 0xCDU}, {20.0F, 0xCCU}, {25.0F, 0xCEU},
}};

[[nodiscard]] const std::vector<EnemyDeathSpawn>& death_table() {
    static const auto table = [] {
        std::vector<EnemyDeathSpawn> out;
        for (const auto& step : kDeathSchedule) {
            for (const auto& s : event_spawns()) {
                if (s.code != step.code || s.place != EventPlace::joint) continue;
                out.push_back({step.tick, s.code, s.kind, s.id, s.joint, s.mode});
            }
        }
        return out;
    }();
    return table;
}

// Spawned records follow their joint for as long as they can live.
constexpr float kFollowTicks = 90.0F;

// Spawn once per (actor, index); later steps update the spawned instance.
// actor_events() holds only the current step, so the bridge keeps the
// instances it created in the session.
void emit_joint_event(Session& session, std::uint8_t actor, std::size_t index, std::size_t state,
                      float spawn_frame, float frame, const Matrix4& world) {
    auto& runtime = *session.effect_runtime;
    const auto key = (static_cast<std::uint32_t>(actor) << 16U) | static_cast<std::uint32_t>(index & 0xFFFFU);
    auto& spawned = session.enemy_effect_actors;
    if (session.enemy_effect_generation != runtime.reset_count()) {
        spawned.clear();
        session.enemy_effect_generation = runtime.reset_count();
    }
    const auto it = std::find_if(spawned.begin(), spawned.end(),
                                 [key](const auto& entry) { return entry.first == key; });
    DynamicActorEvent out;
    out.actor = actor;
    out.actor_state = static_cast<std::uint16_t>(state);
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

std::span<const EnemyEventSpawn> em000_event_spawns() noexcept { return event_spawns(); }

std::span<const EnemyAttackEvent> em000_command_events(std::string_view enemy_class) noexcept {
    static const auto per_class = [] {
        std::array<std::vector<EnemyAttackEvent>, std::size(kClassTables)> out;
        for (std::size_t c = 0U; c < std::size(kClassTables); ++c) {
            const auto& pairs = kClassTables[c].pairs;
            for (const auto& e : kCommandEvents) {
                const std::size_t at = (e.table == 2U ? kTableEntries : 0U) + e.command;
                if (e.command >= kTableEntries || at >= pairs.size()) continue;
                out[c].push_back({pairs[at][0], pairs[at][1], e.frame, e.code});
            }
        }
        return out;
    }();
    for (std::size_t c = 0U; c < std::size(kClassTables); ++c) {
        if (kClassTables[c].name == enemy_class) return per_class[c];
    }
    return {};
}

std::span<const EnemyAttackEvent> em000_attack_events() noexcept { return em000_command_events("CEm000"); }

std::span<const EnemyDeathSpawn> em000_death_spawns() noexcept { return death_table(); }

std::span<const EffectBinding> em000_effect_bindings() noexcept {
    // One binding per handler spawn (state: its row of em000_event_spawns),
    // then one per death spawn. All end through their own records.
    static const auto bindings = [] {
        std::vector<EffectBinding> out;
        const auto make = [](std::uint8_t actor, char kind, std::uint16_t id, std::uint16_t state) {
            return EffectBinding{actor, kind, id, kEm000EffectSlot, RuntimeEffectParent::DynamicActor,
                                 state, 0xFFU, 0xFFU, 0xFFU, EvidenceStatus::EXE_AND_CORPUS_CONFIRMED,
                                 EffectLifetimeRule::EffectCallback, EvidenceStatus::EXE_CONFIRMED,
                                 std::span<const EffectChildRef>{}, 0U};
        };
        const auto& spawns = event_spawns();
        for (std::size_t i = 0U; i < spawns.size(); ++i) {
            out.push_back(make(kEm000WeaponActor, spawns[i].kind, spawns[i].id, static_cast<std::uint16_t>(i)));
        }
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
        // Every event of the playing action up to this frame, each spawn of
        // its code where the handler puts it (the enemy's origin for the
        // actor and given positions; joints past the body are skipped).
        const auto& spawns = event_spawns();
        for_each_em000_event(action->bank, action->action, -1.0F, frame,
                             [&](std::size_t index, const EnemyAttackEvent& event) {
            for (std::size_t row = 0U; row < spawns.size(); ++row) {
                const auto& s = spawns[row];
                if (s.code != event.code) continue;
                std::optional<Matrix4> world;
                if (s.place == EventPlace::joint) {
                    world = body_joint_effect_world(*session, s.joint, s.mode, s.scale);
                } else {
                    Matrix4 origin;
                    origin.values = {s.scale, 0.0F, 0.0F, 0.0F, 0.0F, s.scale, 0.0F, 0.0F,
                                     0.0F, 0.0F, s.scale, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F};
                    world = origin;
                }
                if (!world.has_value()) continue;
                emit_joint_event(*session, kEm000WeaponActor, index * 256U + row, row, event.frame, frame, *world);
            }
        }, session->enemy_class);
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
            emit_joint_event(*session, kEm000DeathActor, i, i, spawn_frame, frame, *world);
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
