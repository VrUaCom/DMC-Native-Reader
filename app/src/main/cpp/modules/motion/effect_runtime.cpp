#include "dmcresource/motion/effect_runtime.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>

#include "dmcresource/resource_session.h"

namespace dmcresource::motion {
namespace {

constexpr std::uint16_t kAnyActorState = 0xFFFFU;
constexpr std::uint8_t kAnyByte = 0xFFU;

[[nodiscard]] bool can_materialize(EvidenceStatus status) noexcept {
    // Resource presence alone is not a runtime call, and structural or
    // undecoded records must remain inspection data. Only a proven EXE
    // consumer/factory binding may create a presentable instance.
    return status == EvidenceStatus::EXE_CONFIRMED ||
           status == EvidenceStatus::EXE_AND_CORPUS_CONFIRMED;
}

}  // namespace

EffectRuntime::EffectRuntime(std::span<const EffectBinding> bindings) {
    set_bindings(bindings);
}

void EffectRuntime::set_bindings(std::span<const EffectBinding> bindings) {
    bindings_.assign(bindings.begin(), bindings.end());
}

void EffectRuntime::set_resources(std::span<const EffectResourceRef> resources) {
    resources_.assign(resources.begin(), resources.end());
    resource_gate_enabled_ = true;
}

void EffectRuntime::begin_step() noexcept {
    actor_events_.clear();
    effect_events_.clear();
}

void EffectRuntime::reset() noexcept {
    actor_events_.clear();
    effect_events_.clear();
    instances_.clear();
    active_instances_.clear();
    next_instance_id_ = 1U;
    next_actor_instance_id_ = 1U;
}

bool EffectRuntime::matches(const EffectBinding& binding,
                            const DynamicActorEvent& event) const noexcept {
    if (binding.actor != event.actor) return false;
    if (binding.actor_state != kAnyActorState &&
        binding.actor_state != event.actor_state) return false;
    if (binding.lane != kAnyByte && binding.lane != event.lane) return false;
    if (binding.channel != kAnyByte && binding.channel != event.channel) return false;
    if (binding.signal_value != kAnyByte &&
        binding.signal_value != event.signal_value) return false;
    return true;
}

bool EffectRuntime::resource_available(const EffectBinding& binding) const noexcept {
    if (!resource_gate_enabled_) return true;
    const auto contains = [this](char kind,
                                 std::uint16_t id,
                                 std::uint32_t slot,
                                 EvidenceStatus evidence) {
        return can_materialize(evidence) &&
               std::any_of(resources_.begin(), resources_.end(),
                           [kind, id, slot](const EffectResourceRef& resource) {
                               return resource.effect_kind == kind &&
                                      resource.effect_id == id &&
                                      resource.resource_slot == slot &&
                                      can_materialize(resource.evidence);
                           });
    };
    if (!contains(binding.effect_kind, binding.effect_id,
                  binding.resource_slot, binding.evidence)) {
        return false;
    }
    const auto contains_tree = [&contains](const auto& self,
                                           const EffectChildRef& child) -> bool {
        if (!contains(child.effect_kind, child.effect_id,
                      child.resource_slot, child.evidence)) {
            return false;
        }
        return std::all_of(
            child.children.begin(), child.children.end(),
            [&self](const EffectChildRef& nested) {
                return self(self, nested);
            });
    };
    return std::all_of(
        binding.children.begin(), binding.children.end(),
        [&contains_tree](const EffectChildRef& child) {
            return contains_tree(contains_tree, child);
        });
}

void EffectRuntime::emit(RuntimeEffectEvent::Kind kind,
                         const RuntimeEffectInstance& instance) {
    effect_events_.push_back(RuntimeEffectEvent{kind, instance.instance_id, instance});
}

void EffectRuntime::apply_actor_event(DynamicActorEvent event) {
    if (event.actor_instance == 0U && event.kind == DynamicActorEventKind::Spawn) {
        event.actor_instance = next_actor_instance_id_++;
    }
    actor_events_.push_back(event);

    if (event.kind == DynamicActorEventKind::Spawn) {
        for (const auto& binding : bindings_) {
            if (!matches(binding, event) ||
                event.evidence == EvidenceStatus::REJECTED ||
                !can_materialize(binding.evidence) ||
                !can_materialize(event.evidence) ||
                !resource_available(binding)) {
                continue;
            }
            RuntimeEffectInstance instance;
            instance.instance_id = next_instance_id_++;
            instance.actor_instance = event.actor_instance;
            instance.actor = event.actor;
            instance.source.effect_kind = binding.effect_kind;
            instance.source.effect_id = binding.effect_id;
            instance.source.resource_slot = binding.resource_slot;
            instance.source.parent = binding.parent;
            instance.source.world = event.world;
            instance.source.world_authoritative = event.world_authoritative;
            instance.source.requires_gameplay_world_context =
                event.requires_gameplay_world_context;
            instance.source.actor_state = event.actor_state;
            instance.source.lane = event.lane;
            instance.source.channel = event.channel;
            instance.source.signal_value = event.signal_value;
            instance.source.script_frame = event.script_frame;
            instance.source.evidence = binding.evidence;
            instance.source.lifetime = binding.lifetime;
            instance.source.lifetime_evidence = binding.lifetime_evidence;
            instance.source.children = binding.children;
            instance.age = 0.0F;
            instance.current_frame = event.script_frame;
            instance.active = event.world_authoritative;
            instance.state = event.world_authoritative
                ? EffectRuntimeState::Active
                : EffectRuntimeState::DeferredTransform;
            instances_.push_back(instance);
            emit(event.world_authoritative
                     ? RuntimeEffectEvent::Kind::Spawn
                     : RuntimeEffectEvent::Kind::Deferred,
                 instance);
        }
        rebuild_active_instances();
        return;
    }

    for (auto& instance : instances_) {
        if (instance.actor_instance != event.actor_instance ||
            instance.actor != event.actor) {
            continue;
        }

        if (event.kind == DynamicActorEventKind::Retire) {
            if (instance.state == EffectRuntimeState::Retired) continue;
            instance.active = false;
            instance.state = EffectRuntimeState::Retired;
            emit(RuntimeEffectEvent::Kind::Retire, instance);
            continue;
        }

        // Update is also the point at which a deferred actor-domain matrix may
        // become available. Until then it remains a non-presentable record.
        if (event.world_authoritative) {
            const float age = event.script_frame - instance.source.script_frame;
            instance.source.world = event.world;
            instance.source.world_authoritative = true;
            instance.source.requires_gameplay_world_context =
                instance.source.requires_gameplay_world_context ||
                event.requires_gameplay_world_context;
            instance.age = std::isfinite(age) && age > 0.0F ? age : 0.0F;
            instance.current_frame = event.script_frame;
            if (!instance.active) {
                instance.active = true;
                instance.state = EffectRuntimeState::Active;
                emit(RuntimeEffectEvent::Kind::Spawn, instance);
            } else {
                emit(RuntimeEffectEvent::Kind::Update, instance);
            }
        }
    }
    rebuild_active_instances();
}

void EffectRuntime::rebuild_active_instances() {
    active_instances_.clear();
    for (const auto& instance : instances_) {
        if (instance.active) active_instances_.push_back(instance);
    }
}

std::span<const RuntimeEffectInstance> EffectRuntime::active_instances() const noexcept {
    return active_instances_;
}

std::span<const RuntimeEffectInstance>
EffectRuntime::presentation_instances() const noexcept {
    return presentation_enabled_ ? std::span<const RuntimeEffectInstance>{active_instances_}
                                  : std::span<const RuntimeEffectInstance>{};
}

bool set_script_effect_bindings(
    dmcresource::Session* session,
    std::span<const EffectBinding> bindings) noexcept {
    if (session == nullptr || bindings.empty()) return false;
    try {
        session->script_effect_bindings.assign(bindings.begin(), bindings.end());
        return true;
    } catch (...) {
        return false;
    }
}

bool ensure_effect_runtime(dmcresource::Session* session) noexcept {
    if (session == nullptr || session->script_effect_bindings.empty()) return false;
    try {
        if (session->effect_runtime == nullptr) {
            session->effect_runtime = std::make_shared<EffectRuntime>(
                std::span<const EffectBinding>{session->script_effect_bindings});
        } else {
            session->effect_runtime->reset();
            session->effect_runtime->set_bindings(
                std::span<const EffectBinding>{session->script_effect_bindings});
        }
        session->effect_runtime->set_resources(session->effect_resources);
        session->effect_runtime->set_presentation_enabled(session->effects_visible);
        return true;
    } catch (...) {
        return false;
    }
}

}  // namespace dmcresource::motion
