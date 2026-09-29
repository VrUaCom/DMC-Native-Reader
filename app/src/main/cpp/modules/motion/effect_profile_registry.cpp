#include "dmcresource/motion/effect_runtime.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <string_view>

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

std::span<const EffectBinding> em034_effect_bindings() noexcept {
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
    static constexpr std::array<EffectBinding, 5> kBindings{{
        {0U, 'V', 463U, 28U, RuntimeEffectParent::DynamicActor,
         0xFFFFU, 1U, 0U, 1U, EvidenceStatus::EXE_AND_CORPUS_CONFIRMED,
         EffectLifetimeRule::ParentActorRetire,
         EvidenceStatus::EXE_CONFIRMED,
         std::span<const EffectChildRef>{kV463Children}},
        {2U, 'V', 423U, 28U, RuntimeEffectParent::DynamicActor,
         0xFFFFU, 1U, 0U, 1U, EvidenceStatus::EXE_AND_CORPUS_CONFIRMED,
         EffectLifetimeRule::ParentActorRetire,
         EvidenceStatus::EXE_CONFIRMED,
         std::span<const EffectChildRef>{kV423Children}},
        {4U, 'V', 488U, 28U, RuntimeEffectParent::DynamicActor,
         0xFFFFU, 1U, 0U, 1U, EvidenceStatus::EXE_AND_CORPUS_CONFIRMED,
         EffectLifetimeRule::ParentActorRetire,
         EvidenceStatus::EXE_CONFIRMED,
         std::span<const EffectChildRef>{kV488Children}},
        {4U, 'V', 475U, 28U, RuntimeEffectParent::DynamicActor,
         0xFFFFU, 1U, 0U, 1U, EvidenceStatus::EXE_AND_CORPUS_CONFIRMED,
         EffectLifetimeRule::ParentActorRetire,
         EvidenceStatus::EXE_CONFIRMED,
         std::span<const EffectChildRef>{kV475Children}},
        {5U, 'V', 276U, 28U, RuntimeEffectParent::DynamicActor,
         0xFFFFU, 1U, 0U, 1U, EvidenceStatus::EXE_AND_CORPUS_CONFIRMED,
         EffectLifetimeRule::ParentActorRetire,
         EvidenceStatus::EXE_CONFIRMED,
         std::span<const EffectChildRef>{kV276Children}},
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
        return set_script_effect_bindings(session, em034_effect_bindings());
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

constexpr std::array<EffectProfileProvider, 1> kProviders{{
    {"em034", &em034_profile_matches, &em034_effect_bindings},
}};

}  // namespace

std::span<const EffectProfileProvider> effect_profile_providers() noexcept {
    return kProviders;
}

bool install_effect_bindings(dmcresource::Session* session) noexcept {
    if (session == nullptr) return false;

    session->script_effect_bindings.clear();
    session->script_effect_child_groups.clear();
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
        return set_script_effect_bindings(session, bindings);
    }
    return false;
}

}  // namespace dmcresource::motion
