#include "dmcresource/motion/effect_runtime.h"

#include <array>
#include <cassert>
#include <cstdint>
#include <vector>

#include "dmcresource/resource_session.h"

int main() {
    using namespace dmcresource::motion;

    constexpr EffectBinding binding{
        2U, 'V', 423U, 28U, RuntimeEffectParent::DynamicActor,
        0xFFFFU, 1U, 0U, 1U, EvidenceStatus::EXE_AND_CORPUS_CONFIRMED,
        EffectLifetimeRule::PreservedUndecoded,
        EvidenceStatus::PRESERVED_UNDECODED};
    EffectRuntime runtime(std::span<const EffectBinding>{&binding, 1U});

    dmcresource::Matrix4 exact_world;
    exact_world.values[12] = 12.0F;
    exact_world.values[13] = 3.0F;

    // Lane isolation: the confirmed binding is lane1/channel0 only.
    runtime.begin_step();
    runtime.apply_actor_event(DynamicActorEvent{
        .kind = DynamicActorEventKind::Spawn,
        .actor = 2U,
        .actor_state = 0U,
        .lane = 0U,
        .channel = 0U,
        .signal_value = 1U,
        .actor_instance = 1U,
        .script_frame = 4.0F,
        .world = exact_world,
        .world_authoritative = true,
        .evidence = EvidenceStatus::EXE_AND_CORPUS_CONFIRMED});
    assert(runtime.effect_events().empty());
    assert(runtime.active_instances().empty());

    runtime.begin_step();
    runtime.apply_actor_event(DynamicActorEvent{
        .kind = DynamicActorEventKind::Spawn,
        .actor = 2U,
        .actor_state = 0U,
        .lane = 1U,
        .channel = 0U,
        .signal_value = 1U,
        .actor_instance = 2U,
        .script_frame = 4.0F,
        .world = exact_world,
        .world_authoritative = true,
        .requires_gameplay_world_context = true,
        .evidence = EvidenceStatus::EXE_AND_CORPUS_CONFIRMED});
    assert(runtime.effect_events().size() == 1U);
    assert(runtime.effect_events()[0].kind == RuntimeEffectEvent::Kind::Spawn);
    assert(runtime.effect_events()[0].instance.source.effect_kind == 'V');
    assert(runtime.effect_events()[0].instance.source.effect_id == 423U);
    assert(runtime.effect_events()[0].instance.source.resource_slot == 28U);
    assert(runtime.effect_events()[0].instance.source.world_authoritative);
    assert(runtime.effect_events()[0].instance.source.requires_gameplay_world_context);
    assert(runtime.effect_events()[0].instance.source.lifetime ==
           EffectLifetimeRule::PreservedUndecoded);
    assert(runtime.active_instances().size() == 1U);

    // Update is stateful and does not invent a timer; age follows the script
    // timeline until the owner emits its canonical retire event.
    runtime.begin_step();
    exact_world.values[12] = 14.0F;
    runtime.apply_actor_event(DynamicActorEvent{
        .kind = DynamicActorEventKind::Update,
        .actor = 2U,
        .actor_instance = 2U,
        .script_frame = 9.0F,
        .world = exact_world,
        .world_authoritative = true,
        .evidence = EvidenceStatus::EXE_AND_CORPUS_CONFIRMED});
    assert(runtime.effect_events().size() == 1U);
    assert(runtime.effect_events()[0].kind == RuntimeEffectEvent::Kind::Update);
    assert(runtime.effect_events()[0].instance.age == 5.0F);
    assert(runtime.effect_events()[0].instance.source.world.values[12] == 14.0F);

    // Presentation visibility does not alter runtime state.
    runtime.set_presentation_enabled(false);
    assert(!runtime.presentation_enabled());
    assert(runtime.active_instances().size() == 1U);
    assert(runtime.presentation_instances().empty());
    runtime.set_presentation_enabled(true);
    assert(runtime.presentation_instances().size() == 1U);

    runtime.begin_step();
    runtime.apply_actor_event(DynamicActorEvent{
        .kind = DynamicActorEventKind::Retire,
        .actor = 2U,
        .actor_instance = 2U,
        .script_frame = 10.0F,
        .world = dmcresource::Matrix4{},
        .world_authoritative = false,
        .evidence = EvidenceStatus::EXE_AND_CORPUS_CONFIRMED});
    assert(runtime.effect_events().size() == 1U);
    assert(runtime.effect_events()[0].kind == RuntimeEffectEvent::Kind::Retire);
    assert(runtime.active_instances().empty());

    // Retire is terminal: a later authoritative update is retained as input
    // but cannot resurrect the retired effect instance.
    runtime.begin_step();
    runtime.apply_actor_event(DynamicActorEvent{
        .kind = DynamicActorEventKind::Update,
        .actor = 2U,
        .actor_instance = 2U,
        .script_frame = 11.0F,
        .world = exact_world,
        .world_authoritative = true,
        .evidence = EvidenceStatus::EXE_AND_CORPUS_CONFIRMED});
    assert(runtime.effect_events().empty());
    assert(runtime.active_instances().empty());

    // When a Session supplies a canonical bank catalog, the exact kind/id and
    // source slot must exist before a confirmed binding can materialize.
    constexpr EffectResourceRef present_resource{
        'V', 423U, 28U, EvidenceStatus::EXE_AND_CORPUS_CONFIRMED};
    runtime.set_resources(std::span<const EffectResourceRef>{&present_resource, 1U});
    runtime.begin_step();
    runtime.apply_actor_event(DynamicActorEvent{
        .kind = DynamicActorEventKind::Spawn,
        .actor = 2U,
        .actor_state = 0U,
        .lane = 1U,
        .channel = 0U,
        .signal_value = 1U,
        .actor_instance = 5U,
        .script_frame = 4.0F,
        .world = exact_world,
        .world_authoritative = true,
        .evidence = EvidenceStatus::EXE_AND_CORPUS_CONFIRMED});
    assert(runtime.effect_events().size() == 1U);
    assert(runtime.effect_events()[0].kind == RuntimeEffectEvent::Kind::Spawn);

    constexpr EffectResourceRef wrong_slot{
        'V', 423U, 29U, EvidenceStatus::EXE_AND_CORPUS_CONFIRMED};
    EffectRuntime missing_resource(std::span<const EffectBinding>{&binding, 1U});
    missing_resource.set_resources(std::span<const EffectResourceRef>{&wrong_slot, 1U});
    missing_resource.begin_step();
    missing_resource.apply_actor_event(DynamicActorEvent{
        .kind = DynamicActorEventKind::Spawn,
        .actor = 2U,
        .actor_state = 0U,
        .lane = 1U,
        .channel = 0U,
        .signal_value = 1U,
        .actor_instance = 6U,
        .script_frame = 4.0F,
        .world = exact_world,
        .world_authoritative = true,
        .evidence = EvidenceStatus::EXE_AND_CORPUS_CONFIRMED});
    assert(missing_resource.effect_events().empty());
    assert(missing_resource.active_instances().empty());

    // A corpus/structural record is retained as evidence but cannot become a
    // runtime instance without an EXE-confirmed consumer/factory binding.
    constexpr EffectBinding structural_binding{
        2U, 'V', 999U, 28U, RuntimeEffectParent::DynamicActor,
        0xFFFFU, 1U, 0U, 1U, EvidenceStatus::STRUCTURAL_CONFIRMED,
        EffectLifetimeRule::PreservedUndecoded,
        EvidenceStatus::PRESERVED_UNDECODED};
    EffectRuntime structural(std::span<const EffectBinding>{&structural_binding, 1U});
    structural.begin_step();
    structural.apply_actor_event(DynamicActorEvent{
        .kind = DynamicActorEventKind::Spawn,
        .actor = 2U,
        .actor_state = 0U,
        .lane = 1U,
        .channel = 0U,
        .signal_value = 1U,
        .actor_instance = 4U,
        .script_frame = 1.0F,
        .world = exact_world,
        .world_authoritative = true,
        .evidence = EvidenceStatus::EXE_AND_CORPUS_CONFIRMED});
    assert(structural.effect_events().empty());
    assert(structural.active_instances().empty());

    // No exact parent matrix means a preserved binding is deferred, never
    // materialized at identity. An exact later update may promote it.
    constexpr EffectBinding deferred_binding{
        4U, 'V', 488U, 28U, RuntimeEffectParent::DynamicActor,
        0xFFFFU, 1U, 0U, 1U, EvidenceStatus::EXE_AND_CORPUS_CONFIRMED,
        EffectLifetimeRule::PreservedUndecoded,
        EvidenceStatus::PRESERVED_UNDECODED};
    runtime.set_bindings(std::span<const EffectBinding>{&deferred_binding, 1U});
    constexpr EffectResourceRef deferred_resource{
        'V', 488U, 28U, EvidenceStatus::EXE_AND_CORPUS_CONFIRMED};
    runtime.set_resources(std::span<const EffectResourceRef>{&deferred_resource, 1U});
    runtime.reset();
    runtime.begin_step();
    runtime.apply_actor_event(DynamicActorEvent{
        .kind = DynamicActorEventKind::Spawn,
        .actor = 4U,
        .actor_state = 0U,
        .lane = 1U,
        .channel = 0U,
        .signal_value = 1U,
        .actor_instance = 3U,
        .script_frame = 2.0F,
        .world = dmcresource::Matrix4{},
        .world_authoritative = false,
        .evidence = EvidenceStatus::EXE_AND_CORPUS_CONFIRMED});
    assert(runtime.effect_events().size() == 1U);
    assert(runtime.effect_events()[0].kind == RuntimeEffectEvent::Kind::Deferred);
    assert(runtime.active_instances().empty());

    runtime.begin_step();
    runtime.apply_actor_event(DynamicActorEvent{
        .kind = DynamicActorEventKind::Update,
        .actor = 4U,
        .actor_instance = 3U,
        .script_frame = 3.0F,
        .world = exact_world,
        .world_authoritative = true,
        .evidence = EvidenceStatus::EXE_AND_CORPUS_CONFIRMED});
    assert(runtime.effect_events().size() == 1U);
    assert(runtime.effect_events()[0].kind == RuntimeEffectEvent::Kind::Spawn);
    assert(runtime.active_instances().size() == 1U);

    // Reset + replay uses the same canonical instance identity and event kind.
    runtime.reset();
    runtime.begin_step();
    runtime.apply_actor_event(DynamicActorEvent{
        .kind = DynamicActorEventKind::Spawn,
        .actor = 4U,
        .actor_state = 0U,
        .lane = 1U,
        .channel = 0U,
        .signal_value = 1U,
        .actor_instance = 3U,
        .script_frame = 2.0F,
        .world = dmcresource::Matrix4{},
        .world_authoritative = false,
        .evidence = EvidenceStatus::EXE_AND_CORPUS_CONFIRMED});
    assert(runtime.effect_events().size() == 1U);
    assert(runtime.effect_events()[0].kind == RuntimeEffectEvent::Kind::Deferred);
    assert(runtime.effect_events()[0].instance.instance_id == 1U);

    // A binding absent from the confirmed table is preserved as no runtime
    // effect, rather than substituted with another FXBANK record.
    EffectRuntime unknown;
    unknown.begin_step();
    unknown.apply_actor_event(DynamicActorEvent{
        .kind = DynamicActorEventKind::Spawn,
        .actor = 99U,
        .actor_state = 0U,
        .lane = 1U,
        .channel = 0U,
        .signal_value = 1U,
        .actor_instance = 1U,
        .script_frame = 1.0F,
        .world = exact_world,
        .world_authoritative = true,
        .evidence = EvidenceStatus::PRESERVED_UNDECODED});
    assert(unknown.effect_events().empty());
    assert(unknown.active_instances().empty());

    // The same canonical event history reaches the same current instance
    // whether the frame is advanced sequentially or replayed after a reset.
    EffectRuntime sequential(std::span<const EffectBinding>{&binding, 1U});
    dmcresource::Matrix4 timeline_world = exact_world;
    sequential.apply_actor_event(DynamicActorEvent{
        .kind = DynamicActorEventKind::Spawn,
        .actor = 2U,
        .actor_state = 0U,
        .lane = 1U,
        .channel = 0U,
        .signal_value = 1U,
        .actor_instance = 7U,
        .script_frame = 4.0F,
        .world = timeline_world,
        .world_authoritative = true,
        .evidence = EvidenceStatus::EXE_AND_CORPUS_CONFIRMED});
    timeline_world.values[12] = 10.0F;
    sequential.apply_actor_event(DynamicActorEvent{
        .kind = DynamicActorEventKind::Update,
        .actor = 2U,
        .actor_instance = 7U,
        .script_frame = 10.0F,
        .world = timeline_world,
        .world_authoritative = true,
        .evidence = EvidenceStatus::EXE_AND_CORPUS_CONFIRMED});
    timeline_world.values[12] = 45.0F;
    sequential.apply_actor_event(DynamicActorEvent{
        .kind = DynamicActorEventKind::Update,
        .actor = 2U,
        .actor_instance = 7U,
        .script_frame = 45.0F,
        .world = timeline_world,
        .world_authoritative = true,
        .evidence = EvidenceStatus::EXE_AND_CORPUS_CONFIRMED});
    assert(sequential.active_instances().size() == 1U);

    EffectRuntime replayed(std::span<const EffectBinding>{&binding, 1U});
    replayed.apply_actor_event(DynamicActorEvent{
        .kind = DynamicActorEventKind::Spawn,
        .actor = 2U,
        .actor_state = 0U,
        .lane = 1U,
        .channel = 0U,
        .signal_value = 1U,
        .actor_instance = 7U,
        .script_frame = 4.0F,
        .world = exact_world,
        .world_authoritative = true,
        .evidence = EvidenceStatus::EXE_AND_CORPUS_CONFIRMED});
    replayed.apply_actor_event(DynamicActorEvent{
        .kind = DynamicActorEventKind::Update,
        .actor = 2U,
        .actor_instance = 7U,
        .script_frame = 45.0F,
        .world = timeline_world,
        .world_authoritative = true,
        .evidence = EvidenceStatus::EXE_AND_CORPUS_CONFIRMED});
    assert(replayed.active_instances().size() == 1U);
    assert(replayed.active_instances()[0].current_frame ==
           sequential.active_instances()[0].current_frame);
    assert(replayed.active_instances()[0].age == sequential.active_instances()[0].age);
    assert(replayed.active_instances()[0].source.world.values[12] ==
           sequential.active_instances()[0].source.world.values[12]);

    replayed.reset();
    replayed.apply_actor_event(DynamicActorEvent{
        .kind = DynamicActorEventKind::Spawn,
        .actor = 2U,
        .actor_state = 0U,
        .lane = 1U,
        .channel = 0U,
        .signal_value = 1U,
        .actor_instance = 7U,
        .script_frame = 4.0F,
        .world = exact_world,
        .world_authoritative = true,
        .evidence = EvidenceStatus::EXE_AND_CORPUS_CONFIRMED});
    timeline_world.values[12] = 10.0F;
    replayed.apply_actor_event(DynamicActorEvent{
        .kind = DynamicActorEventKind::Update,
        .actor = 2U,
        .actor_instance = 7U,
        .script_frame = 10.0F,
        .world = timeline_world,
        .world_authoritative = true,
        .evidence = EvidenceStatus::EXE_AND_CORPUS_CONFIRMED});
    assert(replayed.active_instances()[0].current_frame == 10.0F);
    assert(replayed.active_instances()[0].age == 6.0F);

    // Nested V dependencies are part of the exact resource graph. The root
    // and the immediate V child are insufficient when the grandchild catalog
    // key is absent.
    const EffectChildRef v8_grandchild{
        'E', 10U, 28U, 1U,
        {0.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 0.0F}, {2.0F, 2.0F, 2.0F},
        EvidenceStatus::EXE_AND_CORPUS_CONFIRMED, 0};
    const std::array<EffectChildRef, 1> v8_children{{v8_grandchild}};
    const EffectChildRef v8_child{
        'V', 8U, 28U, 3U,
        {0.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 0.0F}, {1.0F, 1.0F, 1.0F},
        EvidenceStatus::EXE_AND_CORPUS_CONFIRMED, 2,
        std::span<const EffectChildRef>{v8_children}};
    const std::array<EffectChildRef, 1> nested_children{{v8_child}};
    const EffectBinding nested_binding{
        4U, 'V', 488U, 28U, RuntimeEffectParent::DynamicActor,
        0xFFFFU, 1U, 0U, 1U, EvidenceStatus::EXE_AND_CORPUS_CONFIRMED,
        EffectLifetimeRule::PreservedUndecoded,
        EvidenceStatus::PRESERVED_UNDECODED,
        std::span<const EffectChildRef>{nested_children}};
    constexpr std::array<EffectResourceRef, 2> missing_nested_resources{{
        {'V', 488U, 28U, EvidenceStatus::EXE_AND_CORPUS_CONFIRMED},
        {'V', 8U, 28U, EvidenceStatus::EXE_AND_CORPUS_CONFIRMED},
    }};
    constexpr std::array<EffectResourceRef, 3> complete_nested_resources{{
        {'V', 488U, 28U, EvidenceStatus::EXE_AND_CORPUS_CONFIRMED},
        {'V', 8U, 28U, EvidenceStatus::EXE_AND_CORPUS_CONFIRMED},
        {'E', 10U, 28U, EvidenceStatus::EXE_AND_CORPUS_CONFIRMED},
    }};
    EffectRuntime nested_runtime(
        std::span<const EffectBinding>{&nested_binding, 1U});
    nested_runtime.set_resources(missing_nested_resources);
    nested_runtime.begin_step();
    nested_runtime.apply_actor_event(DynamicActorEvent{
        .kind = DynamicActorEventKind::Spawn,
        .actor = 4U,
        .actor_state = 0U,
        .lane = 1U,
        .channel = 0U,
        .signal_value = 1U,
        .actor_instance = 1U,
        .script_frame = 0.0F,
        .world = exact_world,
        .world_authoritative = true,
        .evidence = EvidenceStatus::EXE_AND_CORPUS_CONFIRMED});
    assert(nested_runtime.effect_events().empty());
    nested_runtime.set_resources(complete_nested_resources);
    nested_runtime.begin_step();
    nested_runtime.apply_actor_event(DynamicActorEvent{
        .kind = DynamicActorEventKind::Spawn,
        .actor = 4U,
        .actor_state = 0U,
        .lane = 1U,
        .channel = 0U,
        .signal_value = 1U,
        .actor_instance = 1U,
        .script_frame = 0.0F,
        .world = exact_world,
        .world_authoritative = true,
        .evidence = EvidenceStatus::EXE_AND_CORPUS_CONFIRMED});
    assert(nested_runtime.effect_events().size() == 1U);
    assert(nested_runtime.effect_events()[0].kind == RuntimeEffectEvent::Kind::Spawn);

    // The Shl02 grenade-side V423 graph is gated by every exact child key in
    // em034_028, including P337 in the same source bank. A wrong source slot
    // on one child must reject the whole graph instead of silently drawing a
    // partial or substituted effect.
    constexpr std::array<EffectChildRef, 3> v423_children{{
        {'E', 752U, 28U, 1U,
         {60.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 0.0F}, {1.0F, 1.0F, 1.0F},
         EvidenceStatus::EXE_AND_CORPUS_CONFIRMED, 0},
        {'E', 887U, 28U, 1U,
         {60.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 0.0F}, {1.0F, 1.0F, 1.0F},
         EvidenceStatus::EXE_AND_CORPUS_CONFIRMED, 3},
        {'P', 337U, 28U, 0U,
         {60.0F, 0.0F, 0.0F}, {0.0F, 90.0F, 0.0F}, {1.0F, 1.0F, 1.0F},
         EvidenceStatus::EXE_AND_CORPUS_CONFIRMED, 0},
    }};
    const EffectBinding v423_binding{
        2U, 'V', 423U, 28U, RuntimeEffectParent::DynamicActor,
        0xFFFFU, 1U, 0U, 1U, EvidenceStatus::EXE_AND_CORPUS_CONFIRMED,
        EffectLifetimeRule::ParentActorRetire,
        EvidenceStatus::EXE_CONFIRMED,
        std::span<const EffectChildRef>{v423_children}};
    constexpr std::array<EffectResourceRef, 4> v423_resources{{
        {'V', 423U, 28U, EvidenceStatus::EXE_AND_CORPUS_CONFIRMED},
        {'E', 752U, 28U, EvidenceStatus::EXE_AND_CORPUS_CONFIRMED},
        {'E', 887U, 28U, EvidenceStatus::EXE_AND_CORPUS_CONFIRMED},
        {'P', 337U, 28U, EvidenceStatus::EXE_AND_CORPUS_CONFIRMED},
    }};
    EffectRuntime v423_runtime(
        std::span<const EffectBinding>{&v423_binding, 1U});
    v423_runtime.set_resources(v423_resources);
    v423_runtime.begin_step();
    v423_runtime.apply_actor_event(DynamicActorEvent{
        .kind = DynamicActorEventKind::Spawn,
        .actor = 2U,
        .actor_state = 0x56U,
        .lane = 1U,
        .channel = 0U,
        .signal_value = 1U,
        .actor_instance = 1U,
        .script_frame = 4.0F,
        .world = exact_world,
        .world_authoritative = true,
        .requires_gameplay_world_context = true,
        .evidence = EvidenceStatus::EXE_AND_CORPUS_CONFIRMED});
    assert(v423_runtime.effect_events().size() == 1U);
    assert(v423_runtime.active_instances().size() == 1U);

    // A non-Lady profile uses the same installation boundary. The runtime
    // does not inspect archive names or semantic effect labels; the profile
    // supplies its confirmed binding and the Session supplies its FXBANK
    // catalog.
    constexpr EffectBinding other_profile_binding{
        7U, 'P', 18U, 41U, RuntimeEffectParent::DynamicActor,
        0xFFFFU, 0U, 2U, 3U, EvidenceStatus::EXE_AND_CORPUS_CONFIRMED,
        EffectLifetimeRule::ParentActorRetire,
        EvidenceStatus::EXE_CONFIRMED};
    dmcresource::Session other_profile;
    other_profile.effect_resources.push_back(
        {'P', 18U, 41U, EvidenceStatus::EXE_AND_CORPUS_CONFIRMED});
    assert(set_script_effect_bindings(
        &other_profile,
        std::span<const EffectBinding>{&other_profile_binding, 1U}));
    assert(ensure_effect_runtime(&other_profile));
    assert(other_profile.effect_runtime != nullptr);
    other_profile.effect_runtime->begin_step();
    other_profile.effect_runtime->apply_actor_event(DynamicActorEvent{
        .kind = DynamicActorEventKind::Spawn,
        .actor = 7U,
        .actor_state = 4U,
        .lane = 0U,
        .channel = 2U,
        .signal_value = 3U,
        .actor_instance = 1U,
        .script_frame = 6.0F,
        .world = exact_world,
        .world_authoritative = true,
        .evidence = EvidenceStatus::EXE_AND_CORPUS_CONFIRMED});
    assert(other_profile.effect_runtime->effect_events().size() == 1U);
    assert(other_profile.effect_runtime->effect_events()[0].instance.source.effect_kind == 'P');
    assert(other_profile.effect_runtime->effect_events()[0].instance.source.effect_id == 18U);

    // Generic profile registry: em034 is selected only by profile identity
    // plus its exact FXBANK slot. Unknown profiles remain effect-free.
    dmcresource::Session lady_profile;
    lady_profile.archive_name = "scr/em034.pac";
    lady_profile.effect_bank_slots.push_back(28U);
    assert(!effect_profile_providers().empty());
    assert(install_effect_bindings(&lady_profile));
    assert(lady_profile.script_effect_bindings.size() == 5U);
    assert(lady_profile.script_effect_bindings[0].effect_kind == 'V');
    assert(lady_profile.script_effect_bindings[0].effect_id == 463U);

    dmcresource::Session unknown_profile;
    unknown_profile.archive_name = "scr/em999.pac";
    unknown_profile.effect_bank_slots.push_back(28U);
    assert(!install_effect_bindings(&unknown_profile));
    assert(unknown_profile.script_effect_bindings.empty());

    // Pass 01: nested binding spans are copied into runtime-owned storage.
    // The provider-owned vectors are then destroyed; the runtime must retain
    // the exact child graph and resource provenance without dangling spans.
    std::vector<EffectChildRef> transient_nested;
    transient_nested.push_back(EffectChildRef{
        'E', 10U, 28U, 1U,
        {0.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 0.0F},
        {1.0F, 1.0F, 1.0F},
        EvidenceStatus::EXE_AND_CORPUS_CONFIRMED, 0});
    std::vector<EffectChildRef> transient_children;
    transient_children.push_back(EffectChildRef{
        'V', 8U, 28U, 3U,
        {0.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 0.0F},
        {1.0F, 1.0F, 1.0F},
        EvidenceStatus::EXE_AND_CORPUS_CONFIRMED, 0,
        std::span<const EffectChildRef>{
            transient_nested.data(), transient_nested.size()}});
    const EffectBinding transient_binding{
        2U, 'V', 423U, 28U, RuntimeEffectParent::DynamicActor,
        0xFFFFU, 1U, 0U, 1U,
        EvidenceStatus::EXE_AND_CORPUS_CONFIRMED,
        EffectLifetimeRule::ParentActorRetire,
        EvidenceStatus::EXE_CONFIRMED,
        std::span<const EffectChildRef>{
            transient_children.data(), transient_children.size()}};
    constexpr std::array<EffectResourceRef, 3> transient_resources{{
        {'V', 423U, 28U, EvidenceStatus::EXE_AND_CORPUS_CONFIRMED},
        {'V', 8U, 28U, EvidenceStatus::EXE_AND_CORPUS_CONFIRMED},
        {'E', 10U, 28U, EvidenceStatus::EXE_AND_CORPUS_CONFIRMED},
    }};
    EffectRuntime owned_graph_runtime(
        std::span<const EffectBinding>{&transient_binding, 1U});
    owned_graph_runtime.set_resources(transient_resources);
    transient_nested.clear();
    transient_children.clear();
    assert(owned_graph_runtime.bindings().size() == 1U);
    assert(owned_graph_runtime.bindings()[0].children.size() == 1U);
    assert(owned_graph_runtime.bindings()[0].children[0].children.size() == 1U);
    assert(owned_graph_runtime.bindings()[0].children[0].children[0].effect_id == 10U);
    owned_graph_runtime.begin_step();
    owned_graph_runtime.apply_actor_event(DynamicActorEvent{
        .kind = DynamicActorEventKind::Spawn,
        .actor = 2U,
        .lane = 1U,
        .channel = 0U,
        .signal_value = 1U,
        .actor_instance = 1U,
        .script_frame = 1.0F,
        .world = exact_world,
        .world_authoritative = true,
        .evidence = EvidenceStatus::EXE_AND_CORPUS_CONFIRMED});
    assert(owned_graph_runtime.effect_events().size() == 1U);

    // The Session registration boundary owns the same nested graph before
    // the generic EffectRuntime is constructed.
    std::vector<EffectChildRef> session_children;
    session_children.push_back(EffectChildRef{
        'E', 752U, 28U, 1U,
        {60.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 0.0F},
        {1.0F, 1.0F, 1.0F},
        EvidenceStatus::EXE_AND_CORPUS_CONFIRMED, 0});
    const EffectBinding session_binding{
        2U, 'V', 423U, 28U, RuntimeEffectParent::DynamicActor,
        0xFFFFU, 1U, 0U, 1U,
        EvidenceStatus::EXE_AND_CORPUS_CONFIRMED,
        EffectLifetimeRule::ParentActorRetire,
        EvidenceStatus::EXE_CONFIRMED,
        std::span<const EffectChildRef>{
            session_children.data(), session_children.size()}};
    dmcresource::Session owned_session;
    owned_session.effect_resources = {
        {'V', 423U, 28U, EvidenceStatus::EXE_AND_CORPUS_CONFIRMED},
        {'E', 752U, 28U, EvidenceStatus::EXE_AND_CORPUS_CONFIRMED},
    };
    assert(set_script_effect_bindings(
        &owned_session,
        std::span<const EffectBinding>{&session_binding, 1U}));
    session_children.clear();
    assert(owned_session.script_effect_bindings.size() == 1U);
    assert(owned_session.script_effect_bindings[0].children.size() == 1U);
    assert(owned_session.script_effect_bindings[0].children[0].effect_id == 752U);
    assert(ensure_effect_runtime(&owned_session));
    assert(owned_session.effect_runtime != nullptr);
    assert(owned_session.effect_runtime->bindings().size() == 1U);
    assert(owned_session.effect_runtime->bindings()[0].children.size() == 1U);
    assert(owned_session.effect_runtime->bindings()[0].children[0].effect_id == 752U);

    return 0;
}
