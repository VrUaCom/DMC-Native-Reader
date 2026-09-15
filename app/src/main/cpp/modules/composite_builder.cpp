#include "dmcresource/composite_builder.h"

#include <string>

#include "dmcresource/composite_placement.h"
#include "dmcresource/mod_attachment_resolver.h"

namespace dmcresource::composite_builder {

BuildResult build_mod_composite(
    const std::vector<const Session*>& parts,
    const std::vector<std::string>& names,
    const BuildOptions& options) noexcept {
    BuildResult out;
    if (parts.size() < 2U || parts.size() != names.size() ||
        options.primary_host_index >= parts.size()) {
        return out;
    }

    try {
        // Existing source-scene flattening remains a low-level compatibility
        // primitive. Production callers enter through this builder so placement
        // policy is no longer owned by generic Session/JNI code.
        out.session = dmcresource::compose_mod_sessions(parts, names);
        if (!out.session) return out;

        if (!options.resolve_default_joint_attachments) {
            if (!out.session->trace.empty()) out.session->trace += "\n";
            out.session->trace += "[OK] composite.builder placement=source-only";
            return out;
        }

        const auto host_index = options.primary_host_index;
        const auto& host = out.session->composite_parts[host_index];

        for (std::size_t child_index = 0U;
             child_index < out.session->composite_parts.size();
             ++child_index) {
            if (child_index == host_index) continue;
            ++out.stats.attachment_attempts;

            const auto resolved = mod_attachment_resolver::resolve_default_joint(
                out.session->composite_parts[child_index], host);
            if (!resolved.ok()) {
                ++out.stats.attachments_unresolved;
                continue;
            }

            const auto placed = composite_placement::attach_to_host_joint(
                out.session.get(), host_index, child_index, resolved.selector);
            if (!placed.ok() || placed.root_matrix.values != resolved.root_matrix.values) {
                if (placed.ok()) {
                    (void)composite_placement::reset_to_source_coordinates(
                        out.session.get(), child_index);
                }
                ++out.stats.attachments_unresolved;
                continue;
            }
            ++out.stats.attachments_resolved;
        }

        if (!out.session->detail.empty()) out.session->detail += "\n";
        out.session->detail +=
            "CompositeBuilder: primaryHost=" + std::to_string(host_index) +
            " defaultJointAttempts=" + std::to_string(out.stats.attachment_attempts) +
            " resolved=" + std::to_string(out.stats.attachments_resolved) +
            " unresolved=" + std::to_string(out.stats.attachments_unresolved);
        if (!out.session->trace.empty()) out.session->trace += "\n";
        out.session->trace += "[OK] composite.builder default-joint-resolution";
        return out;
    } catch (...) {
        out.session.reset();
        out.stats = {};
        return out;
    }
}

}  // namespace dmcresource::composite_builder
