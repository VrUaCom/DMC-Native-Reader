#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "dmcresource/inspection_document.h"
#include "dmcresource/render_scene.h"

namespace dmcresource::motion {

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

enum class RuntimeEffectState : std::uint8_t {
    DeferredTransform,
    Active,
    Retired,
    Rejected,
};

enum class RuntimeEffectEventKind : std::uint8_t {
    Spawn,
    Retire,
    Rejected,
};

enum class RuntimeEffectLifetime : std::uint8_t {
    PreservedUndecoded,
    OwnerActor,
    GameplayWorldContext,
};

struct EffectResourceKey final {
    char kind{};
    std::uint16_t id{};

    [[nodiscard]] constexpr bool operator==(const EffectResourceKey&) const noexcept = default;
};

struct EffectChildLink final {
    EffectResourceKey key;
    std::uint8_t dispatch{};
    std::array<float, 3> translation{};
    std::array<float, 3> rotation_degrees{};
    std::array<float, 3> scale{1.0F, 1.0F, 1.0F};
};

struct EffectCatalogRecord final {
    EffectResourceKey key;
    std::uint32_t source_slot{};
    bool payload_present{};
    std::vector<EffectChildLink> children;
};

struct RuntimeEffectSpawn final {
    char effect_kind{'V'};
    std::uint32_t effect_id{};
    std::uint32_t resource_slot{};

    RuntimeEffectParent parent{RuntimeEffectParent::World};
    Matrix4 world{};
    bool transform_deferred{};

    std::int8_t owner_actor{-1};
    std::uint16_t actor_state{};
    std::uint8_t lane{};
    std::uint8_t channel{};
    float script_frame{};

    EvidenceLevel evidence{EvidenceLevel::Unknown};
    RuntimeEffectLifetime lifetime{RuntimeEffectLifetime::PreservedUndecoded};
};

struct RuntimeEffectInstance final {
    std::uint64_t instance_id{};
    RuntimeEffectSpawn source;
    float age{};
    bool active{};
    RuntimeEffectState state{RuntimeEffectState::Rejected};
};

struct RuntimeEffectEvent final {
    RuntimeEffectEventKind kind{RuntimeEffectEventKind::Rejected};
    std::uint64_t instance_id{};
    RuntimeEffectSpawn source;
    float frame{};
};

struct DynamicActorEvent final {
    std::int8_t actor{-1};
    bool spawned{};
    float frame{};
};

struct RuntimeStepResult final {
    bool ok{};
    bool replayed{};
    std::vector<DynamicActorEvent> actor_events;
    std::vector<RuntimeEffectEvent> effect_events;
    std::string detail;
};

class EffectRuntime final {
public:
    [[nodiscard]] bool load_bank(
        std::span<const std::uint8_t> bytes,
        std::uint32_t source_slot) noexcept;

    [[nodiscard]] bool loaded() const noexcept { return loaded_; }
    [[nodiscard]] std::uint32_t source_slot() const noexcept { return source_slot_; }
    [[nodiscard]] const std::vector<EffectCatalogRecord>& catalog() const noexcept {
        return catalog_;
    }
    [[nodiscard]] const EffectCatalogRecord* find(char kind, std::uint16_t id) const noexcept;
    [[nodiscard]] bool dependencies_ready(char kind, std::uint16_t id) const noexcept;

    [[nodiscard]] std::optional<std::uint64_t> spawn(RuntimeEffectSpawn source) noexcept;
    void record_actor_event(std::int8_t actor, bool spawned, float frame) noexcept;
    void retire_owner(std::int8_t actor, float frame) noexcept;
    void update(float frame) noexcept;
    void reset() noexcept;
    void clear_pending_events() noexcept;

    [[nodiscard]] std::vector<RuntimeEffectEvent> consume_effect_events();
    [[nodiscard]] std::vector<DynamicActorEvent> consume_actor_events();
    [[nodiscard]] const std::vector<RuntimeEffectInstance>& active_instances() const noexcept {
        return instances_;
    }

    void set_presentation_visible(bool visible) noexcept { presentation_visible_ = visible; }
    [[nodiscard]] bool presentation_visible() const noexcept { return presentation_visible_; }

private:
    bool loaded_{};
    bool presentation_visible_{true};
    std::uint32_t source_slot_{};
    std::uint64_t next_instance_id_{1U};
    float current_frame_{};
    std::vector<EffectCatalogRecord> catalog_;
    std::vector<RuntimeEffectInstance> instances_;
    std::vector<RuntimeEffectEvent> pending_effect_events_;
    std::vector<DynamicActorEvent> pending_actor_events_;
};

// CEm034-confirmed actor-owned FX roots. Returns the number of spawned
// instances. Unknown actor/lane/channel/value combinations are deliberately
// rejected by returning zero; there is no guessed effect fallback.
[[nodiscard]] std::size_t spawn_lady_actor_effects(
    EffectRuntime& runtime,
    std::int8_t actor,
    std::uint16_t actor_state,
    std::uint8_t lane,
    std::uint8_t channel,
    std::uint8_t value,
    float frame,
    const Matrix4* exact_world = nullptr) noexcept;

}  // namespace dmcresource::motion
