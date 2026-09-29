#pragma once

#include <cstddef>
#include <cstdint>
#include <array>
#include <span>
#include <string_view>
#include <vector>

#include "dmcresource/render_scene.h"

namespace dmcresource {
struct Session;
}

namespace dmcresource::motion {

using ScriptEffectPrepareFn = bool (*)(dmcresource::Session*) noexcept;
using ScriptEffectResetFn = void (*)(dmcresource::Session*) noexcept;
using ScriptEffectStepFn = void (*)(dmcresource::Session*, float) noexcept;

// Optional profile-owned event producer. It is deliberately a narrow hook:
// profile code emits only EXE/corpus-confirmed DynamicActorEvent records, and
// the shared EffectRuntime owns matching, resource gating, replay and retire.
struct ScriptEffectBridge final {
    ScriptEffectPrepareFn prepare{};
    ScriptEffectResetFn reset{};
    ScriptEffectStepFn step{};
};

// Evidence is carried with the runtime binding so a presentation layer cannot
// silently promote a structural or undecoded record into a named effect.
enum class EvidenceStatus : std::uint8_t {
    EXE_CONFIRMED,
    CORPUS_CONFIRMED,
    EXE_AND_CORPUS_CONFIRMED,
    STRUCTURAL_CONFIRMED,
    SEMANTIC_CANDIDATE,
    PRESERVED_UNDECODED,
    REJECTED,
};

// Resource identity is the EXE loader key, not a synthetic filename or a
// physical record ordinal. `resource_slot` remains provenance for the bank
// that supplied the key.
struct EffectResourceRef final {
    char effect_kind{};
    std::uint16_t effect_id{};
    std::uint32_t resource_slot{};
    EvidenceStatus evidence{EvidenceStatus::PRESERVED_UNDECODED};
};

// One EXE-dispatched child in a V composite. The transform stays in the
// corpus's local translation/degree-rotation/scale domain; presentation code
// composes it with the runtime parent only after resolving the child key.
struct EffectChildRef final {
    char effect_kind{};
    std::uint16_t effect_id{};
    std::uint32_t resource_slot{};
    std::uint8_t dispatch_kind{};  // 0=P, 1=E, 2=G, 3=V
    std::array<float, 3> translation{};
    std::array<float, 3> rotation_degrees{};
    std::array<float, 3> scale{1.0F, 1.0F, 1.0F};
    EvidenceStatus evidence{EvidenceStatus::PRESERVED_UNDECODED};
    // V's update consumer compares the signed u16 at entry +0x08 against
    // its own local accumulator. This is retained as corpus/EXE data, but is
    // intentionally not converted to a script-frame timer: the V local clock
    // has not been proven identical to MotionScript frame units.
    std::int16_t activation_offset{};
    // Recursive V children keep their canonical dependency graph. A nested
    // graph is checked by the resource gate; it is not flattened into a
    // synthetic filename or semantic effect name.
    std::span<const EffectChildRef> children{};
};

enum class RuntimeEffectParent : std::uint8_t {
    World,
    CharacterRoot,
    BodyJoint,
    PersistentComponent,
    DynamicActor,
    RuntimeMatrix,
    ProjectileTransform,
    TetherPoint,
};

enum class EffectRuntimeState : std::uint8_t {
    DeferredTransform,
    Active,
    Retired,
};

// Lifetime is deliberately separate from the binding identity. A factory
// call can be EXE+corpus confirmed while the child graph's retire callback is
// still unresolved. Such a binding remains replayable, but the runtime must
// not invent a countdown or animation-end rule for it.
enum class EffectLifetimeRule : std::uint8_t {
    PreservedUndecoded,
    ParentActorRetire,
    ParentStateTransition,
    ScriptSignal,
    FixedCountdown,
    AnimationEnd,
    EffectCallback,
    WorldCollision,
    ManualExeRetire,
};

enum class DynamicActorEventKind : std::uint8_t {
    Spawn,
    Update,
    Retire,
};

struct DynamicActorEvent final {
    DynamicActorEventKind kind{DynamicActorEventKind::Spawn};
    std::uint8_t actor{};  // profile-local actor identity, e.g. CEm034Shl02
    std::uint16_t actor_state{};
    std::uint8_t lane{0xFFU};
    std::uint8_t channel{0xFFU};
    std::uint8_t signal_value{0xFFU};
    std::uint64_t actor_instance{};
    float script_frame{};
    Matrix4 world{};
    // Identity is never a spatial fallback. False means the event is retained
    // for replay/inspection but cannot materialize an effect instance yet.
    bool world_authoritative{};
    // True when the EXE actor update needs a gameplay-world service (target,
    // collision or query manager) after the canonical spawn event. This is a
    // context requirement, not permission to synthesize a standalone path.
    bool requires_gameplay_world_context{};
    EvidenceStatus evidence{EvidenceStatus::PRESERVED_UNDECODED};
};

// A binding is data, not a character-specific branch in MotionPlayer. The
// profile bridge supplies these records after EXE/corpus reverse is complete.
struct EffectBinding final {
    std::uint8_t actor{};
    char effect_kind{};  // FXBANK canonical identity uses (kind, u16 id).
    std::uint16_t effect_id{};
    std::uint32_t resource_slot{};
    RuntimeEffectParent parent{RuntimeEffectParent::DynamicActor};
    std::uint16_t actor_state{0xFFFFU};
    std::uint8_t lane{0xFFU};
    std::uint8_t channel{0xFFU};
    std::uint8_t signal_value{0xFFU};
    EvidenceStatus evidence{EvidenceStatus::PRESERVED_UNDECODED};
    EffectLifetimeRule lifetime{EffectLifetimeRule::PreservedUndecoded};
    EvidenceStatus lifetime_evidence{EvidenceStatus::PRESERVED_UNDECODED};
    std::span<const EffectChildRef> children{};
};

// Generic profile registry. Each provider owns only evidence-backed
// profile matching and binding data; the shared runtime owns execution.
using EffectProfileMatchFn = bool (*)(const dmcresource::Session*) noexcept;
using EffectProfileBindingsFn = std::span<const EffectBinding> (*)() noexcept;

struct EffectProfileProvider final {
    std::string_view profile_id;
    EffectProfileMatchFn matches{};
    EffectProfileBindingsFn bindings{};
};

[[nodiscard]] std::span<const EffectProfileProvider>
effect_profile_providers() noexcept;

struct RuntimeEffectSpawn final {
    char effect_kind{};
    std::uint32_t effect_id{};
    std::uint32_t resource_slot{};

    RuntimeEffectParent parent{RuntimeEffectParent::DynamicActor};
    Matrix4 world{};
    bool world_authoritative{};
    bool requires_gameplay_world_context{};

    std::uint16_t actor_state{};
    std::uint8_t lane{0xFFU};
    std::uint8_t channel{0xFFU};
    std::uint8_t signal_value{0xFFU};

    float script_frame{};
    EvidenceStatus evidence{EvidenceStatus::PRESERVED_UNDECODED};
    EffectLifetimeRule lifetime{EffectLifetimeRule::PreservedUndecoded};
    EvidenceStatus lifetime_evidence{EvidenceStatus::PRESERVED_UNDECODED};
    std::span<const EffectChildRef> children{};
};

struct RuntimeEffectInstance final {
    std::uint64_t instance_id{};
    std::uint64_t actor_instance{};
    std::uint8_t actor{};
    RuntimeEffectSpawn source{};
    float age{};
    float current_frame{};
    bool active{};
    EffectRuntimeState state{EffectRuntimeState::DeferredTransform};
};

struct RuntimeEffectEvent final {
    enum class Kind : std::uint8_t {
        Deferred,
        Spawn,
        Update,
        Retire,
    };

    Kind kind{Kind::Deferred};
    std::uint64_t instance_id{};
    RuntimeEffectInstance instance{};
};

// Kept generic for the public script-runtime boundary. Character bridges can
// fill component transitions independently from actor/effect events.
struct ComponentTransition final {
    std::uint8_t component{};
    std::uint8_t placement{};
    std::uint8_t control_domain{};
    float uniform_scale{1.0F};
};

struct RuntimeStepResult final {
    std::vector<ComponentTransition> component_changes;
    std::vector<DynamicActorEvent> actor_events;
    std::vector<RuntimeEffectEvent> effect_events;
};

class EffectRuntime final {
public:
    explicit EffectRuntime(std::span<const EffectBinding> bindings = {});

    void set_bindings(std::span<const EffectBinding> bindings);
    // Enables exact resource provenance checking. Before this is supplied,
    // the runtime remains usable as a profile/unit-test event model; a
    // Session-backed runtime always supplies the parsed FXBANK catalog.
    void set_resources(std::span<const EffectResourceRef> resources);
    [[nodiscard]] bool has_bindings() const noexcept { return !bindings_.empty(); }
    [[nodiscard]] std::span<const EffectBinding> bindings() const noexcept {
        return bindings_;
    }
    [[nodiscard]] std::span<const EffectResourceRef> resources() const noexcept {
        return resources_;
    }

    // Starts one deterministic runtime step. Reset/replay may then append all
    // events up to a sought frame into this step's event stream.
    void begin_step() noexcept;
    void reset() noexcept;
    void apply_actor_event(DynamicActorEvent event);

    void set_presentation_enabled(bool enabled) noexcept {
        presentation_enabled_ = enabled;
    }
    [[nodiscard]] bool presentation_enabled() const noexcept {
        return presentation_enabled_;
    }

    [[nodiscard]] std::span<const DynamicActorEvent> actor_events() const noexcept {
        return actor_events_;
    }
    [[nodiscard]] std::span<const RuntimeEffectEvent> effect_events() const noexcept {
        return effect_events_;
    }
    [[nodiscard]] std::span<const RuntimeEffectInstance> instances() const noexcept {
        return instances_;
    }
    [[nodiscard]] std::span<const RuntimeEffectInstance> active_instances() const noexcept;
    // Presentation-only projection. Turning effects off must leave the
    // internal active instance/lifetime state untouched; a renderer may use
    // this view without accidentally changing replay semantics.
    [[nodiscard]] std::span<const RuntimeEffectInstance>
    presentation_instances() const noexcept;

private:
    [[nodiscard]] bool matches(const EffectBinding& binding,
                               const DynamicActorEvent& event) const noexcept;
    [[nodiscard]] bool resource_available(const EffectBinding& binding) const noexcept;
    void emit(RuntimeEffectEvent::Kind kind,
              const RuntimeEffectInstance& instance);
    void rebuild_active_instances();

    std::vector<EffectBinding> bindings_;
    std::vector<EffectResourceRef> resources_;
    std::vector<DynamicActorEvent> actor_events_;
    std::vector<RuntimeEffectInstance> instances_;
    std::vector<RuntimeEffectInstance> active_instances_;
    std::vector<RuntimeEffectEvent> effect_events_;
    std::uint64_t next_instance_id_{1U};
    std::uint64_t next_actor_instance_id_{1U};
    bool resource_gate_enabled_{};
    bool presentation_enabled_{true};
};

// Profile-neutral registration boundary. A character/profile reverse bridge
// supplies only evidence-backed bindings; the shared Script Play path then
// consumes them for every controller. This stores the profile table but does
// not start playback or fabricate a runtime instance.
[[nodiscard]] bool set_script_effect_bindings(
    dmcresource::Session* session,
    std::span<const EffectBinding> bindings) noexcept;

// Select the first matching generic profile provider. Unknown profiles remain
// effect-free; no archive-name/MOT/effect-name guess is allowed.
[[nodiscard]] bool install_effect_bindings(
    dmcresource::Session* session) noexcept;

// Rebuild/configure the session-owned runtime from the profile bindings and
// the session's canonical FXBANK resource catalog. An empty binding set is
// intentionally not materialized.
[[nodiscard]] bool ensure_effect_runtime(
    dmcresource::Session* session) noexcept;

}  // namespace dmcresource::motion
