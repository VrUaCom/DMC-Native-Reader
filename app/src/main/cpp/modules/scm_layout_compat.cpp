#include "dmcresource/scm_layout_compat.h"

#include <algorithm>
#include <cstdint>
#include <string_view>
#include <utility>
#include <vector>

#include "dmc_rengine/formats/scm_layout.hpp"

namespace dmcresource::scm_layout_compat {
namespace {

namespace scm = dmc::rengine::formats::scm;

constexpr std::uint64_t kHeaderBytes = 0x40U;
constexpr std::uint64_t kMeshRecordBytes = 0x50U;

[[nodiscard]] bool placement_error(std::string_view code) noexcept {
    return code == "scm.mesh-layout-mismatch" || code == "scm.mesh-table-layout-mismatch";
}

}  // namespace

bool readable_despite_placement(const scm::ParseResult& parsed, std::size_t file_size) noexcept {
    try {
        if (!parsed.recognized || parsed.document.objects.empty()) return false;
        bool any_error = false;
        for (const auto& diagnostic : parsed.diagnostics) {
            if (diagnostic.severity != dmc::rengine::formats::ParseSeverity::error) continue;
            if (!placement_error(diagnostic.code)) return false;
            any_error = true;
        }
        if (!any_error) return false;

        // Every byte range the file's records point at: none may leave the
        // file or overlap another, so no stream is read twice or as another.
        std::vector<std::pair<std::uint64_t, std::uint64_t>> ranges;
        const auto add = [&](std::uint64_t offset, std::uint64_t bytes) {
            if (bytes == 0U) return true;
            if (offset < kHeaderBytes || offset > file_size || bytes > file_size - offset) return false;
            ranges.emplace_back(offset, offset + bytes);
            return true;
        };
        for (const auto& object : parsed.document.objects) {
            if (object.meshes.empty()) return false;
            if (!add(object.mesh_table_offset, object.meshes.size() * kMeshRecordBytes)) return false;
            for (const auto& mesh : object.meshes) {
                const std::uint64_t n = mesh.vertex_count;
                if (n == 0U || mesh.positions.size() != n || mesh.normals.size() != n || mesh.uvs.size() != n ||
                    mesh.colors_topology.size() != n) {
                    return false;
                }
                if (!add(mesh.positions_offset, n * 12U) || !add(mesh.normals_offset, n * 12U) ||
                    !add(mesh.uv_offset, n * 4U) || !add(mesh.color_flags_offset, n * 4U)) {
                    return false;
                }
                if (mesh.index_workspace_capacity != scm::index_workspace_capacity_bytes(mesh.vertex_count) ||
                    !add(mesh.index_workspace_offset, mesh.index_workspace_capacity)) {
                    return false;
                }
            }
        }
        std::sort(ranges.begin(), ranges.end());
        for (std::size_t i = 1; i < ranges.size(); ++i) {
            if (ranges[i].first < ranges[i - 1U].second) return false;
        }
        return true;
    } catch (...) {
        return false;
    }
}

}  // namespace dmcresource::scm_layout_compat
