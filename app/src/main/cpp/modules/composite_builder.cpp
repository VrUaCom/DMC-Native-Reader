#include "dmcresource/composite_builder.h"

#include <string>

#include "dmcresource/composite_placement.h"
#include "dmcresource/mod_attachment_resolver.h"

namespace dmcresource::composite_builder {
namespace {

[[nodiscard]] bool assign_workspace_identity(Session* session) noexcept {
    if (session == nullptr || session->composite_parts.empty()) return false;
    for (auto& part : session->composite_parts) {
        const auto asset_id = session->workspace_graph.add_asset(
            ResourceAssetKind::Model, part.name);
        if (!asset_id) return false;
        const auto instance_id = session->workspace_graph.add_model_instance(*asset_id);
        if (!instance_id) return false;
        part.asset_id = *asset_id;
        part.instance_id = *instance_id;
    }
    return session->workspace_graph.valid();
}

}  // namespace

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
        if (!out.session || !assign_workspace_identity(out.session.get())) {
            out.session.reset();
            return out;
        }

        if (!options.resolve_default_joint_attachments) {
            if (!out.session->trace.empty()) out.session->trace += "\n";
            out.session->trace +=
                "[OK] composite.builder placement=source-only stable-ids=1 cpp23=1";
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
            " primaryHostInstance=" + std::to_string(host.instance_id) +
            " defaultJointAttempts=" + std::to_string(out.stats.attachment_attempts) +
            " resolved=" + std::to_string(out.stats.attachments_resolved) +
            " unresolved=" + std::to_string(out.stats.attachments_unresolved) +
            " language=C++23";
        if (!out.session->trace.empty()) out.session->trace += "\n";
        out.session->trace +=
            "[OK] composite.builder default-joint-resolution stable-ids=1 cpp23=1";
        return out;
    } catch (...) {
        out.session.reset();
        out.stats = {};
        return out;
    }
}

}  // namespace dmcresource::composite_builder
