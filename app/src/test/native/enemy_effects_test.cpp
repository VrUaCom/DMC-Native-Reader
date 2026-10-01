// em000 family attack effects: the (action, frame) table recovered from
// emulated CComEm000 commands, the E42 / V42 bindings of event code 3, and the
// shared runtime spawning both on one actor event.
#include "dmcresource/motion/enemy_effects.h"

#include <cassert>
#include <cstdio>
#include <vector>

int main() {
    using namespace dmcresource::motion;
    const auto events = em000_attack_events();
    assert(events.size() == 9U);
    for (const auto& e : events) assert(e.bank == 0U && e.code == 3U && e.frame > 0.0F);

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
    assert(bindings.size() == 2U);
    assert(bindings[0].effect_kind == 'E' && bindings[0].effect_id == 42U && bindings[0].resource_slot == 41U);
    assert(bindings[1].effect_kind == 'V' && bindings[1].effect_id == 42U);

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
    spawn.actor_state = 3U;
    spawn.script_frame = 40.0F;
    spawn.world.values = {2, 0, 0, 0, 0, 2, 0, 0, 0, 0, 2, 0, 10, 20, 30, 1};
    spawn.world_authoritative = true;
    spawn.evidence = EvidenceStatus::EXE_AND_CORPUS_CONFIRMED;
    runtime.apply_actor_event(spawn);
    assert(runtime.active_instances().size() == 2U);
    runtime.advance(45.0F);
    for (const auto& i : runtime.active_instances()) assert(i.age == 5.0F && i.source.world.values[0] == 2.0F);
    std::printf("enemy_effects ok\n");
    return 0;
}
