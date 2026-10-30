// em000 family effects: the tables generated from the emulated handler and
// CComEm000 commands (every code, not only the scythe's 3), the bindings, the
// death schedule built from the handler's spawns, and the shared runtime
// spawning code 3's two records on one actor event.
#include "dmcresource/motion/enemy_effects.h"

#include <cassert>
#include <cstdio>
#include <vector>

int main() {
    using namespace dmcresource::motion;
    // The handler: code 3 puts E42 and V42 on body joint 1, by position,
    // scaled 2; code 0x0A V273 at the enemy; code 1 V105 at the given point.
    const auto spawns = em000_event_spawns();
    const auto find = [&](std::uint8_t code, char kind, std::uint16_t id) {
        for (const auto& s : spawns) if (s.code == code && s.kind == kind && s.id == id) return &s;
        return static_cast<const EnemyEventSpawn*>(nullptr);
    };
    for (const char kind : {'E', 'V'}) {
        const auto* s = find(3U, kind, 42U);
        assert(s != nullptr && s->place == EventPlace::joint && s->joint == 1U && s->mode == 1U && s->scale == 2.0F);
    }
    assert(find(0x0AU, 'V', 273U) != nullptr && find(0x0AU, 'V', 273U)->place == EventPlace::actor);
    assert(find(0x01U, 'V', 105U) != nullptr && find(0x01U, 'V', 105U)->place == EventPlace::given);

    // The commands: the scythe gates of v78 are there, with codes other than 3.
    const auto events = em000_attack_events();
    std::size_t scythe = 0U, other = 0U;
    for (const auto& e : events) {
        assert(e.bank == 0U && e.frame >= 0.0F);
        if (e.code == 3U) ++scythe; else ++other;
    }
    assert(scythe >= 9U && other >= 10U);
    const auto has = [&](std::uint8_t action, float frame, std::uint8_t code) {
        for (const auto& e : events) if (e.action == action && e.frame == frame && e.code == code) return true;
        return false;
    };
    assert(has(62U, 40.0F, 3U) && has(66U, 30.0F, 3U) && has(82U, 76.0F, 3U) && has(84U, 180.0F, 3U));
    assert(has(85U, 100.0F, 3U) && has(87U, 30.0F, 3U) && has(52U, 0.0F, 0x00U) && has(53U, 120.0F, 0x01U));
    // Table 2 (+0x28) commands go through the class's second table.
    assert(has(58U, 0.0F, 0x68U) && has(28U, 0.0F, 0x0EU));
    assert(em000_command_events("CEm005").empty() && !em000_command_events("CEm001").empty());

    std::vector<float> hits;
    for_each_em000_event(0U, 68U, -1.0F, 45.0F, [&](std::size_t, const EnemyAttackEvent& e) { hits.push_back(e.frame); });
    assert(hits.size() == 1U && hits[0] == 40.0F);
    hits.clear();
    for_each_em000_event(0U, 68U, 45.0F, 60.0F, [&](std::size_t, const EnemyAttackEvent& e) { hits.push_back(e.frame); });
    assert(hits.size() == 1U && hits[0] == 50.0F);
    hits.clear();
    for_each_em000_event(0U, 82U, -1.0F, 75.0F, [&](std::size_t, const EnemyAttackEvent& e) { hits.push_back(e.frame); });
    assert(hits.empty());

    const auto bindings = em000_effect_bindings();
    assert(bindings.size() == spawns.size() + 28U);
    for (std::size_t i = 0U; i < spawns.size(); ++i) {
        assert(bindings[i].actor == kEm000WeaponActor && bindings[i].actor_state == i);
        assert(bindings[i].effect_kind == spawns[i].kind && bindings[i].effect_id == spawns[i].id);
        assert(bindings[i].resource_slot == 41U);
    }
    std::uint16_t e42_row = 0U;
    while (!(spawns[e42_row].code == 3U && spawns[e42_row].kind == 'E')) ++e42_row;

    // Death schedule: 28 spawns at ticks 0..25 on body joints 1..21.
    const auto death = em000_death_spawns();
    assert(death.size() == 28U);
    std::size_t at0 = 0U, p32 = 0U;
    for (std::size_t i = 0U; i < death.size(); ++i) {
        const auto& d = death[i];
        assert(d.joint >= 1U && d.joint <= 21U && d.mode <= 1U);
        assert((d.kind == 'P') == (d.mode == 1U));
        if (d.tick == 0.0F) ++at0;
        if (d.kind == 'P') { ++p32; assert(d.id == 32U); }
        const auto& b = bindings[spawns.size() + i];
        assert(b.actor == kEm000DeathActor && b.actor_state == i && b.effect_kind == d.kind && b.effect_id == d.id);
        if (i > 0U) assert(death[i - 1U].tick <= d.tick);
    }
    assert(at0 == 4U && p32 == 20U && death.back().tick == 25.0F && death.front().code == 0x69U);
    assert(class_event_names(nullptr).empty() && !trigger_class_event(nullptr, 0U));

    EffectRuntime runtime(bindings);
    const std::vector<EffectResourceRef> resources{
        {'E', 42U, 41U, EvidenceStatus::EXE_AND_CORPUS_CONFIRMED},
        {'V', 42U, 41U, EvidenceStatus::EXE_AND_CORPUS_CONFIRMED},
    };
    runtime.set_resources(resources);
    runtime.begin_step();
    DynamicActorEvent spawn;
    spawn.kind = DynamicActorEventKind::Spawn;
    spawn.actor = kEm000WeaponActor;
    spawn.actor_state = e42_row;
    spawn.script_frame = 40.0F;
    spawn.world.values = {2, 0, 0, 0, 0, 2, 0, 0, 0, 0, 2, 0, 10, 20, 30, 1};
    spawn.world_authoritative = true;
    spawn.evidence = EvidenceStatus::EXE_AND_CORPUS_CONFIRMED;
    runtime.apply_actor_event(spawn);
    // Code 3's second record (V42) is the next row: one event each.
    assert(spawns[e42_row + 1U].code == 3U && spawns[e42_row + 1U].kind == 'V');
    spawn.actor_state = static_cast<std::uint16_t>(e42_row + 1U);
    runtime.apply_actor_event(spawn);
    assert(runtime.active_instances().size() == 2U);
    runtime.advance(45.0F);
    for (const auto& i : runtime.active_instances()) assert(i.age == 5.0F && i.source.world.values[0] == 2.0F);
    std::printf("enemy_effects ok\n");
    return 0;
}
