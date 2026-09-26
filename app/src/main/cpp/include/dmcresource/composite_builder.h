#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include "dmcresource/resource_session.h"

namespace dmcresource::composite_builder {

struct BuildOptions final {
    // The product supplies an explicit primary/base MOD. Native Reader does not
    // infer host ownership from filenames. Current Android composition maps the
    // already-open model to part 0 and appended MODs to later parts.
    std::size_t primary_host_index{0U};
    // Opt-in only. The canonical executable reads MOD header +0x13
    // (manager +0xFA) solely as a translation probe (0x14031FA80, 0x1402FD040):
    // it never supplies the full root matrix of a companion model's geometry.
    // Companion MODs (hair, coat, accessories) are authored in the character's
    // model space, so the product default keeps source coordinates and reports
    // the selector as a diagnostic instead of moving geometry onto a host joint.
    bool resolve_default_joint_attachments{false};
};

struct BuildStats final {
    std::size_t attachment_attempts{};
    std::size_t attachments_resolved{};
    std::size_t attachments_unresolved{};
};

struct BuildResult final {
    std::unique_ptr<Session> session;
    BuildStats stats;

    [[nodiscard]] explicit operator bool() const noexcept {
        return session != nullptr;
    }
};

// Product-level MOD composition boundary. Low-level source-scene flattening is
// kept behind the session primitive while attachment resolution and placement
// policy live here. This lets the flattening implementation move independently
// without changing Spider/JNI or host-selection semantics.
[[nodiscard]] BuildResult build_mod_composite(
    const std::vector<const Session*>& parts,
    const std::vector<std::string>& names,
    const BuildOptions& options = {}) noexcept;

}  // namespace dmcresource::composite_builder
