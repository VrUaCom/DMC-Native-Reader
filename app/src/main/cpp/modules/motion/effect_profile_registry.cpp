#include "dmcresource/motion/effect_runtime.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <string_view>

#include "dmcresource/motion/enemy_effects.h"
#include "dmcresource/resource_session.h"

namespace dmcresource::motion {
namespace {

// EXE/corpus-confirmed profile matching is kept in the registry, not in the
// shared attachment/state consumer. Unknown profiles remain effect-free.
[[nodiscard]] bool archive_has_stem(std::string_view archive_name,
                                    std::string_view stem) noexcept {
    if (stem.empty() || archive_name.size() < stem.size()) return false;
    for (std::size_t start = 0U;
         start + stem.size() <= archive_name.size(); ++start) {
        bool match = true;
        for (std::size_t i = 0U; i < stem.size(); ++i) {
            const auto lhs = static_cast<unsigned char>(archive_name[start + i]);
            const auto rhs = static_cast<unsigned char>(stem[i]);
            if (std::tolower(lhs) != std::tolower(rhs)) {
                match = false;
                break;
            }
        }
        if (match) return true;
    }
    return false;
}

std::span<const EffectBinding> em034_effect_bindings_impl() noexcept {
    // FXBANK source is em034.pac slot 28 / em034_028.pnst. The V ids are
    // runtime identities, not human effect names. Shl04 owns two distinct
    // bindings; E765 is deliberately not merged with V475 because its
    // lifecycle differs in the canonical actor path. All promoted V roots are
    // actor-owned: their retire contract is the owning Shl actor's canonical
    // lifecycle, not a guessed child timer. Reader may leave the actor
    // deferred when no exact standalone world matrix is available, but it
    // must still preserve this ownership rule for a later retire event.
    constexpr auto confirmed = EvidenceStatus::EXE_AND_CORPUS_CONFIRMED;
    static constexpr std::array<EffectChildRef, 2> kV276Children{{
        {'E', 571U, 28U, 1U,
         {0.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 0.0F}, {1.0F, 1.0F, 1.0F}, confirmed, 0},
        {'E', 571U, 28U, 1U,
         {0.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 90.0F}, {1.0F, 1.0F, 1.0F}, confirmed, 0},
    }};
    static constexpr std::array<EffectChildRef, 3> kV423Children{{
        {'E', 752U, 28U, 1U,
         {60.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 0.0F}, {1.0F, 1.0F, 1.0F}, confirmed, 0},
        {'E', 887U, 28U, 1U,
         {60.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 0.0F}, {1.0F, 1.0F, 1.0F}, confirmed, 3},
        {'P', 337U, 28U, 0U,
         {60.0F, 0.0F, 0.0F}, {0.0F, 90.0F, 0.0F}, {1.0F, 1.0F, 1.0F}, confirmed, 0},
    }};
    static constexpr std::array<EffectChildRef, 1> kV463Children{{
        {'E', 741U, 28U, 1U,
         {0.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 0.0F}, {1.0F, 1.0F, 1.0F}, confirmed, 0},
    }};
    static constexpr std::array<EffectChildRef, 1> kV475Children{{
        {'E', 404U, 28U, 1U,
         {0.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 0.0F}, {1.0F, 1.0F, 1.0F}, confirmed, 0},
    }};
    static constexpr std::array<EffectChildRef, 4> kV8Children{{
        {'E', 10U, 28U, 1U,
         {0.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 0.0F}, {2.0F, 2.0F, 2.0F}, confirmed, 0},
        {'E', 10U, 28U, 1U,
         {0.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 0.0F}, {1.0F, 1.0F, 1.0F}, confirmed, 1},
        {'E', 10U, 28U, 1U,
         {0.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 0.0F}, {1.0F, 1.0F, 1.0F}, confirmed, 2},
        {'E', 2U, 28U, 1U,
         {0.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 0.0F}, {1.0F, 1.0F, 1.0F}, confirmed, 3},
    }};
    static constexpr std::array<EffectChildRef, 6> kV488Children{{
        {'P', 18U, 28U, 0U,
         {0.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 0.0F}, {1.0F, 1.0F, 1.0F}, confirmed, 4},
        {'P', 3U, 28U, 0U,
         {0.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 0.0F}, {1.0F, 1.0F, 1.0F}, confirmed, 2},
        {'P', 16U, 28U, 0U,
         {0.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 0.0F}, {1.0F, 1.0F, 1.0F}, confirmed, 5},
        {'P', 2U, 28U, 0U,
         {0.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 0.0F}, {1.0F, 1.0F, 1.0F}, confirmed, 3},
        {'E', 1U, 28U, 1U,
         {0.0F, 15.0F, 0.0F}, {0.0F, 0.0F, 0.0F}, {1.1F, 1.1F, 1.1F}, confirmed, 0},
        {'V', 8U, 28U, 3U,
         {0.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 0.0F}, {1.0F, 1.0F, 1.0F}, confirmed, 2,
         std::span<const EffectChildRef>{kV8Children}},
    }};
    static constexpr std::array<EffectBinding, 10> kBindings{{
        // CEm034Shl00 init 0x140172380: V463 follows the bullet (+0xC0, mode 3).
        {0U, 'V', 463U, 28U, RuntimeEffectParent::ProjectileTransform,
         0xFFFFU, 1U, 0U, 1U, EvidenceStatus::EXE_AND_CORPUS_CONFIRMED,
         EffectLifetimeRule::ParentActorRetire,
         EvidenceStatus::EXE_CONFIRMED,
         std::span<const EffectChildRef>{kV463Children}},
        // CEm034 spawns V423 next to CEm034Shl02 (0x140169937..0x1401699C5)
        // through 0x1402E7A90(kind 3, 0x1A7, slot20 world, mode 3): a copied
        // muzzle matrix, not the shell. Its E children end by their own
        // record lifetime, independently of the shell's flight.
        {2U, 'V', 423U, 28U, RuntimeEffectParent::RuntimeMatrix,
         0xFFFFU, 1U, 0U, 1U, EvidenceStatus::EXE_AND_CORPUS_CONFIRMED,
         EffectLifetimeRule::EffectCallback,
         EvidenceStatus::EXE_CONFIRMED,
         std::span<const EffectChildRef>{kV423Children}},
        // CEm034Shl04 explode 0x1401753A0: 0x1402E7CA0(3, 0x1E8, &shell+0x1A0,
        // 0x10) copies the grenade matrix (y + 2).
        {4U, 'V', 488U, 28U, RuntimeEffectParent::RuntimeMatrix,
         0xFFFFU, 1U, 0U, 1U, EvidenceStatus::EXE_AND_CORPUS_CONFIRMED,
         EffectLifetimeRule::EffectCallback,
         EvidenceStatus::EXE_CONFIRMED,
         std::span<const EffectChildRef>{kV488Children}, 2U},
        // CEm034Shl04 flight 0x140175785: V475 once the fuse is below 60,
        // following the grenade (mode 3), retired with it.
        {4U, 'V', 475U, 28U, RuntimeEffectParent::ProjectileTransform,
         0xFFFFU, 1U, 0U, 1U, EvidenceStatus::EXE_AND_CORPUS_CONFIRMED,
         EffectLifetimeRule::ParentActorRetire,
         EvidenceStatus::EXE_CONFIRMED,
         std::span<const EffectChildRef>{kV475Children}, 1U},
        // CEm034Shl05 init 0x140175CC0: V276 follows the shot (mode 3).
        {5U, 'V', 276U, 28U, RuntimeEffectParent::ProjectileTransform,
         0xFFFFU, 1U, 0U, 1U, EvidenceStatus::EXE_AND_CORPUS_CONFIRMED,
         EffectLifetimeRule::ParentActorRetire,
         EvidenceStatus::EXE_CONFIRMED,
         std::span<const EffectChildRef>{kV276Children}},
        // CEm034Shl02 init 0x1401738F0: 0x1402E7CA0(3, 0x179, NULL, 0x10),
        // then effect+0xC0 = &shell+0x1A0 and effect+0xD8 = 3, i.e. V377
        // follows the flying shell matrix; state 3 retires it (0x1403261E0
        // on shell+0xD60). Children are decoded from the FXBANK record.
        {2U, 'V', 377U, 28U, RuntimeEffectParent::ProjectileTransform,
         0xFFFFU, 1U, 0U, 1U, EvidenceStatus::EXE_AND_CORPUS_CONFIRMED,
         EffectLifetimeRule::ParentActorRetire,
         EvidenceStatus::EXE_CONFIRMED,
         std::span<const EffectChildRef>{}, 0U},
        // CEm034Shl02 state 2 0x140173800: 0x1402E7CA0(3, 0x21F, &shell+0x1A0,
        // 0x10) copies the shell matrix (translation.y += 2.0).
        {2U, 'V', 543U, 28U, RuntimeEffectParent::RuntimeMatrix,
         0xFFFFU, 1U, 0U, 1U, EvidenceStatus::EXE_AND_CORPUS_CONFIRMED,
         EffectLifetimeRule::EffectCallback,
         EvidenceStatus::EXE_CONFIRMED,
         std::span<const EffectChildRef>{}, 2U},
        // CEm034Shl04 init 0x1401754F0: 0x1402E7A80(1, 0x2FD) is the grenade
        // sprite E765 (+0x84 held), following the grenade matrix (mode 3).
        {4U, 'E', 765U, 28U, RuntimeEffectParent::ProjectileTransform,
         0xFFFFU, 1U, 0U, 1U, EvidenceStatus::EXE_AND_CORPUS_CONFIRMED,
         EffectLifetimeRule::ParentActorRetire,
         EvidenceStatus::EXE_CONFIRMED,
         std::span<const EffectChildRef>{}, 0U},
        // Stage hit of a straight shell: its collider branch with flags & 3
        // (+0x270) enters state 2 and spawns 0x1402E7A80(3, id, &shell+0x1A0),
        // a copy of the shell matrix. Shl00 0x14017273B: V473 (0x1D9).
        {0U, 'V', 473U, 28U, RuntimeEffectParent::RuntimeMatrix,
         0xFFFFU, 1U, 0U, 1U, EvidenceStatus::EXE_AND_CORPUS_CONFIRMED,
         EffectLifetimeRule::EffectCallback,
         EvidenceStatus::EXE_CONFIRMED,
         std::span<const EffectChildRef>{}, 2U},
        // Shl05 0x14017607A: V277 (0x115).
        {5U, 'V', 277U, 28U, RuntimeEffectParent::RuntimeMatrix,
         0xFFFFU, 1U, 0U, 1U, EvidenceStatus::EXE_AND_CORPUS_CONFIRMED,
         EffectLifetimeRule::EffectCallback,
         EvidenceStatus::EXE_CONFIRMED,
         std::span<const EffectChildRef>{}, 2U},
    }};
    return kBindings;
}

bool install_effect_bindings(Session* session) noexcept {
    if (session == nullptr) return false;

    session->script_effect_bindings.clear();

    bool is_em034 = false;
    const std::string_view archive_name = session->archive_name;
    constexpr std::string_view prefix = "em034";
    if (archive_name.size() >= prefix.size()) {
        for (std::size_t start = 0U;
             start + prefix.size() <= archive_name.size(); ++start) {
            bool match = true;
            for (std::size_t i = 0U; i < prefix.size(); ++i) {
                if (std::tolower(static_cast<unsigned char>(
                        archive_name[start + i])) != prefix[i]) {
                    match = false;
                    break;
                }
            }
            if (match) {
                is_em034 = true;
                break;
            }
        }
    }
    if (!is_em034 ||
        std::find(session->effect_bank_slots.begin(),
                  session->effect_bank_slots.end(), 28U) ==
            session->effect_bank_slots.end()) {
        return false;
    }

    try {
        return set_script_effect_bindings(session, em034_effect_bindings_impl());
    } catch (...) {
        return false;
    }
}


[[nodiscard]] bool em034_profile_matches(
    const dmcresource::Session* session) noexcept {
    if (session == nullptr ||
        !archive_has_stem(session->archive_name, "em034")) {
        return false;
    }
    return std::find(session->effect_bank_slots.begin(),
                     session->effect_bank_slots.end(), 28U) !=
           session->effect_bank_slots.end();
}

[[nodiscard]] std::span<const EffectBinding> em000_bindings_provider() noexcept {
    return em000_effect_bindings();
}

constexpr std::array<EffectProfileProvider, 2> kProviders{{
    {"em034", &em034_profile_matches, &em034_effect_bindings_impl},
    {"em000", &em000_profile_matches, &em000_bindings_provider},
}};

}  // namespace

std::span<const EffectBinding> em034_effect_bindings() noexcept {
    return em034_effect_bindings_impl();
}

std::span<const EffectProfileProvider> effect_profile_providers() noexcept {
    return kProviders;
}

bool install_effect_bindings(dmcresource::Session* session) noexcept {
    if (session == nullptr) return false;

    session->script_effect_bindings.clear();
    session->script_effect_child_groups.clear();
    session->script_effect_bridge.step = nullptr;
    session->script_effect_bridge.reset = nullptr;
    if (session->effect_runtime != nullptr) {
        session->effect_runtime->reset();
        session->effect_runtime->set_bindings(
            std::span<const EffectBinding>{});
    }
    for (const auto& provider : effect_profile_providers()) {
        if (provider.matches == nullptr ||
            provider.bindings == nullptr ||
            !provider.matches(session)) {
            continue;
        }
        const auto bindings = provider.bindings();
        if (bindings.empty()) return false;
        // Profiles whose events come from per-frame script action tests get
        // the Script Play step hook; CEm034 runs its own state bridge.
        if (provider.profile_id == "em000") {
            session->script_effect_bridge.step = em000_bridge_step;
            session->script_effect_bridge.reset = em000_bridge_reset;
        }
        return set_script_effect_bindings(session, bindings);
    }
    return false;
}

}  // namespace dmcresource::motion
