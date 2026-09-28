#include "dmcresource/motion/effect_runtime.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <utility>

#include "dmcresource/effect_bank.h"

namespace dmcresource::motion {
namespace {

[[nodiscard]] std::uint16_t u16(
    std::span<const std::uint8_t> bytes,
    std::size_t offset) noexcept {
    if (offset + 2U > bytes.size()) return 0U;
    return static_cast<std::uint16_t>(
        static_cast<std::uint16_t>(bytes[offset]) |
        (static_cast<std::uint16_t>(bytes[offset + 1U]) << 8U));
}

[[nodiscard]] float f32(
    std::span<const std::uint8_t> bytes,
    std::size_t offset) noexcept {
    if (offset + 4U > bytes.size()) return 0.0F;
    const std::uint32_t bits =
        static_cast<std::uint32_t>(bytes[offset]) |
        (static_cast<std::uint32_t>(bytes[offset + 1U]) << 8U) |
        (static_cast<std::uint32_t>(bytes[offset + 2U]) << 16U) |
        (static_cast<std::uint32_t>(bytes[offset + 3U]) << 24U);
    float value = 0.0F;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

[[nodiscard]] char dispatch_kind(std::uint8_t dispatch) noexcept {
    switch (dispatch) {
    case 0U: return 'P';
    case 1U: return 'E';
    case 2U: return 'G';
    case 3U: return 'V';
    default: return '?';
    }
}

[[nodiscard]] std::vector<EffectChildLink> decode_v_children(
    const effect_bank::Record& record) {
    std::vector<EffectChildLink> out;
    if (record.kind != 'V' || record.bytes.size() < 0x30U) return out;

    const auto signed_count =
        static_cast<std::int16_t>(u16(record.bytes, 0U));
    if (signed_count < 0 || signed_count > 64) return out;

    out.reserve(static_cast<std::size_t>(signed_count));
    for (std::int16_t index = 0; index < signed_count; ++index) {
        const std::size_t base = static_cast<std::size_t>(index) * 0x2CU;
        if (base + 0x30U > record.bytes.size()) {
            out.clear();
            return out;
        }

        const auto dispatch = record.bytes[base + 0x04U];
        const auto kind = dispatch_kind(dispatch);
        if (kind == '?') {
            out.clear();
            return out;
        }

        EffectChildLink child;
        child.dispatch = dispatch;
        child.key = {kind, u16(record.bytes, base + 0x06U)};
        child.translation = {
            f32(record.bytes, base + 0x0CU),
            f32(record.bytes, base + 0x10U),
            f32(record.bytes, base + 0x14U),
        };
        child.rotation_degrees = {
            f32(record.bytes, base + 0x18U),
            f32(record.bytes, base + 0x1CU),
            f32(record.bytes, base + 0x20U),
        };
        child.scale = {
            f32(record.bytes, base + 0x24U),
            f32(record.bytes, base + 0x28U),
            f32(record.bytes, base + 0x2CU),
        };
        out.push_back(child);
    }
    return out;
}

[[nodiscard]] RuntimeEffectSpawn lady_spawn(
    std::uint16_t id,
    std::int8_t actor,
    std::uint16_t actor_state,
    std::uint8_t lane,
    std::uint8_t channel,
    float frame,
    const Matrix4* exact_world,
    RuntimeEffectLifetime lifetime) noexcept {
    RuntimeEffectSpawn source;
    source.effect_kind = 'V';
    source.effect_id = id;
    source.parent =
        exact_world != nullptr
            ? RuntimeEffectParent::RuntimeMatrix
            : RuntimeEffectParent::DynamicActor;
    if (exact_world != nullptr) source.world = *exact_world;
    source.transform_deferred = exact_world == nullptr;
    source.owner_actor = actor;
    source.actor_state = actor_state;
    source.lane = lane;
    source.channel = channel;
    source.script_frame = frame;
    source.evidence = EvidenceLevel::ExeAndCorpusConfirmed;
    source.lifetime = lifetime;
    return source;
}

}  // namespace

bool EffectRuntime::load_bank(
    std::span<const std::uint8_t> bytes,
    std::uint32_t source_slot) noexcept {
    try {
        reset();
        catalog_.clear();
        loaded_ = false;
        source_slot_ = source_slot;

        const auto bank = effect_bank::parse_bank(bytes);
        if (!bank || !bank->terminated || bank->records.empty()) return false;

        catalog_.reserve(bank->records.size());
        for (const auto& record : bank->records) {
            if (record.id > 0xFFFFU) {
                catalog_.clear();
                return false;
            }
            EffectCatalogRecord catalog_record;
            catalog_record.key = {
                record.kind,
                static_cast<std::uint16_t>(record.id),
            };
            catalog_record.source_slot = record.slot;
            catalog_record.payload_present = !record.bytes.empty();
            if (record.kind == 'V') {
                catalog_record.children = decode_v_children(record);
                const auto count =
                    record.bytes.size() >= 2U
                        ? static_cast<std::int16_t>(u16(record.bytes, 0U))
                        : static_cast<std::int16_t>(-1);
                if (count < 0 ||
                    static_cast<std::size_t>(count) !=
                        catalog_record.children.size()) {
                    catalog_.clear();
                    return false;
                }
            }
            catalog_.push_back(std::move(catalog_record));
        }

        // Runtime identity is (kind,u16 id). Duplicate identities would make
        // an automatic effect binding ambiguous and are therefore rejected.
        for (std::size_t i = 0U; i < catalog_.size(); ++i) {
            for (std::size_t j = i + 1U; j < catalog_.size(); ++j) {
                if (catalog_[i].key == catalog_[j].key) {
                    catalog_.clear();
                    return false;
                }
            }
        }

        loaded_ = true;
        return true;
    } catch (...) {
        catalog_.clear();
        loaded_ = false;
        return false;
    }
}

const EffectCatalogRecord* EffectRuntime::find(
    char kind,
    std::uint16_t id) const noexcept {
    const auto found = std::find_if(
        catalog_.begin(), catalog_.end(),
        [kind, id](const EffectCatalogRecord& record) {
            return record.key.kind == kind && record.key.id == id;
        });
    return found == catalog_.end() ? nullptr : &*found;
}

bool EffectRuntime::dependencies_ready(
    char kind,
    std::uint16_t id) const noexcept {
    if (!loaded_) return false;
    const auto* root = find(kind, id);
    if (root == nullptr || !root->payload_present) return false;
    if (kind != 'V') return true;

    for (const auto& child : root->children) {
        const auto* resource = find(child.key.kind, child.key.id);
        if (resource == nullptr || !resource->payload_present) return false;
    }
    return true;
}

std::optional<std::uint64_t> EffectRuntime::spawn(
    RuntimeEffectSpawn source) noexcept {
    try {
        const bool evidence_ok =
            source.evidence == EvidenceLevel::ExeAndCorpusConfirmed;
        const auto id =
            source.effect_id <= 0xFFFFU
                ? static_cast<std::uint16_t>(source.effect_id)
                : 0U;
        const auto* root =
            source.effect_id <= 0xFFFFU
                ? find(source.effect_kind, id)
                : nullptr;

        if (!evidence_ok || root == nullptr ||
            !dependencies_ready(source.effect_kind, id)) {
            pending_effect_events_.push_back({
                RuntimeEffectEventKind::Rejected,
                0U,
                source,
                source.script_frame,
            });
            return std::nullopt;
        }

        source.resource_slot = root->source_slot;
        RuntimeEffectInstance instance;
        instance.instance_id = next_instance_id_++;
        instance.source = source;
        instance.active = true;
        instance.state =
            source.transform_deferred
                ? RuntimeEffectState::DeferredTransform
                : RuntimeEffectState::Active;
        const auto instance_id = instance.instance_id;
        instances_.push_back(instance);
        pending_effect_events_.push_back({
            RuntimeEffectEventKind::Spawn,
            instance_id,
            source,
            source.script_frame,
        });
        return instance_id;
    } catch (...) {
        return std::nullopt;
    }
}

void EffectRuntime::record_actor_event(
    std::int8_t actor,
    bool spawned,
    float frame) noexcept {
    if (actor < 0 || !std::isfinite(frame)) return;
    pending_actor_events_.push_back({actor, spawned, frame});
}

void EffectRuntime::retire_owner(
    std::int8_t actor,
    float frame) noexcept {
    if (actor < 0 || !std::isfinite(frame)) return;
    for (auto& instance : instances_) {
        if (!instance.active || instance.source.owner_actor != actor ||
            instance.source.lifetime != RuntimeEffectLifetime::OwnerActor) {
            continue;
        }
        instance.active = false;
        instance.state = RuntimeEffectState::Retired;
        pending_effect_events_.push_back({
            RuntimeEffectEventKind::Retire,
            instance.instance_id,
            instance.source,
            frame,
        });
    }
}

void EffectRuntime::update(float frame) noexcept {
    if (!std::isfinite(frame)) return;
    current_frame_ = frame;
    for (auto& instance : instances_) {
        if (!instance.active) continue;
        instance.age =
            std::max(0.0F, frame - instance.source.script_frame);
    }
}

void EffectRuntime::reset() noexcept {
    instances_.clear();
    pending_effect_events_.clear();
    pending_actor_events_.clear();
    next_instance_id_ = 1U;
    current_frame_ = 0.0F;
}

void EffectRuntime::clear_pending_events() noexcept {
    pending_effect_events_.clear();
    pending_actor_events_.clear();
}

std::vector<RuntimeEffectEvent> EffectRuntime::consume_effect_events() {
    auto out = std::move(pending_effect_events_);
    pending_effect_events_.clear();
    return out;
}

std::vector<DynamicActorEvent> EffectRuntime::consume_actor_events() {
    auto out = std::move(pending_actor_events_);
    pending_actor_events_.clear();
    return out;
}

std::size_t spawn_lady_actor_effects(
    EffectRuntime& runtime,
    std::int8_t actor,
    std::uint16_t actor_state,
    std::uint8_t lane,
    std::uint8_t channel,
    std::uint8_t value,
    float frame,
    const Matrix4* exact_world) noexcept {
    if (lane != 1U || channel != 0U || value != 1U) return 0U;

    std::size_t spawned = 0U;
    const auto spawn = [&](std::uint16_t id,
                           RuntimeEffectLifetime lifetime,
                           const Matrix4* world) {
        if (runtime.spawn(lady_spawn(
                id, actor, actor_state, lane, channel, frame, world, lifetime))) {
            ++spawned;
        }
    };

    switch (actor) {
    case 0:
        // CEm034Shl00 init -> V463. Actor-owned runtime field.
        spawn(463U, RuntimeEffectLifetime::OwnerActor, exact_world);
        break;
    case 2:
        // CEm034 Shl02 factory success -> V423 through 0x1402E7A90 mode3.
        // The exact normalized runtime matrix is supplied by the caller.
        spawn(423U, RuntimeEffectLifetime::GameplayWorldContext, exact_world);
        break;
    case 4:
        // Shl04 owns V475; V488 is a distinct phase effect and therefore does
        // not inherit the V475 owner-retire contract.
        spawn(488U, RuntimeEffectLifetime::PreservedUndecoded, exact_world);
        spawn(475U, RuntimeEffectLifetime::OwnerActor, exact_world);
        break;
    case 5:
        // Shl05 init -> V276. Additional V435/V277 are collision/world gated
        // and intentionally not emitted by standalone Script Play.
        spawn(276U, RuntimeEffectLifetime::PreservedUndecoded, exact_world);
        break;
    default:
        break;
    }
    return spawned;
}

}  // namespace dmcresource::motion
