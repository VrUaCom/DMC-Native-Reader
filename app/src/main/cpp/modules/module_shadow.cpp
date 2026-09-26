#include "dmcresource/native_module.h"

#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <map>
#include <sstream>
#include <string>
#include <utility>

#include "dmcresource/module_support.h"
#include "dmcresource/shadow_hull.h"

namespace dmcresource {
namespace {

[[nodiscard]] std::string selector_list(const shadow::Hull& hull) {
    std::map<std::uint32_t, std::size_t> counts;
    for (const auto selector : hull.selectors) ++counts[selector];
    std::ostringstream out;
    bool first = true;
    for (const auto& [joint, count] : counts) {
        if (!first) out << ", ";
        first = false;
        out << "joint " << joint << " x" << count;
    }
    return out.str();
}

// Standalone SHW view: every hull becomes one primitive so the closed shapes
// can be looked at (and wireframed); the inspection lists the hull table.
PipelineResult run_shw_module(const NativeModule& module,
                              std::string_view,
                              const std::uint8_t* bytes,
                              std::size_t size,
                              const ProbeResult& probe) noexcept {
    try {
        const auto parsed = shadow::parse_hulls(bytes, size);
        if (!parsed) {
            return module_support::reject(probe, module.id,
                                          "SHW rejected by canonical hull parser");
        }
        const auto& set = *parsed;

        PipelineResult out;
        out.accepted = true;
        out.renderable = true;
        out.probe = probe;
        out.modules.push_back({"identity-probe", true});
        out.modules.push_back({"bounded-read-guard", true});
        out.modules.push_back({"canonical.shw.hull-parser", true});
        out.modules.push_back({module.id, true});

        out.inspection.format = "SHW";
        out.inspection.root.id = "shw";
        out.inspection.root.title = "SHW shadow hulls";
        out.inspection.root.kind = InspectionKind::Document;
        out.inspection.root.source_span = SourceSpan{0U, size};
        const auto add = [&out](const char* name, std::string value, EvidenceLevel level) {
            out.inspection.root.properties.push_back({name, std::move(value), level});
        };
        std::ostringstream version;
        version << set.version;
        add("Version(+0x04)", version.str(), EvidenceLevel::DataConfirmed);
        add("Hulls(+0x10)", std::to_string(set.hulls.size()), EvidenceLevel::ExeConfirmed);
        add("ModelNodes(+0x11)", std::to_string(set.node_count), EvidenceLevel::DataConfirmed);
        add("Vertices", std::to_string(set.vertex_count()), EvidenceLevel::ExeConfirmed);
        add("Triangles", std::to_string(set.triangle_count()), EvidenceLevel::ExeConfirmed);
        add("ClosedHulls(T=2V-4)",
            std::to_string(set.closed_hulls()) + "/" + std::to_string(set.hulls.size()),
            EvidenceLevel::StructuralConfirmed);
        add("Selectors", "per-vertex joint of the paired MOD (0x1403202F0)",
            EvidenceLevel::ExeConfirmed);
        add("Shadow", "open the owning PAC to see the hulls cast on the floor",
            EvidenceLevel::Recognized);

        InspectionNode table;
        table.id = "hulls";
        table.title = "Hulls";
        table.kind = InspectionKind::Collection;
        for (std::size_t index = 0U; index < set.hulls.size(); ++index) {
            const auto& hull = set.hulls[index];
            MeshPrimitive primitive;
            primitive.name = "hull " + std::to_string(index);
            primitive.mesh.vertices = hull.vertices;
            primitive.mesh.indices = hull.indices;
            primitive.object_index = static_cast<std::uint32_t>(index);
            out.scene.meshes.push_back(std::move(primitive));

            InspectionNode node;
            node.id = "hull-" + std::to_string(index);
            node.title = "Hull " + std::to_string(index);
            node.kind = InspectionKind::Mesh;
            node.properties.push_back({"Vertices", std::to_string(hull.vertices.size()),
                                       EvidenceLevel::ExeConfirmed});
            node.properties.push_back({"Triangles", std::to_string(hull.indices.size() / 3U),
                                       EvidenceLevel::ExeConfirmed});
            node.properties.push_back({"Joints", selector_list(hull),
                                       EvidenceLevel::ExeConfirmed});
            table.children.push_back(std::move(node));
        }
        out.inspection.root.children.push_back(std::move(table));

        std::ostringstream detail;
        detail << "SHW shadow hulls | hulls=" << set.hulls.size()
               << " vertices=" << set.vertex_count()
               << " triangles=" << set.triangle_count()
               << " closed=" << set.closed_hulls() << "/" << set.hulls.size()
               << " modelNodes=" << static_cast<unsigned>(set.node_count)
               << " maxJoint=" << set.max_selector();
        for (std::size_t index = 0U; index < set.hulls.size(); ++index) {
            const auto& hull = set.hulls[index];
            detail << "\n  hull " << index << ": " << hull.vertices.size() << " vertices, "
                   << hull.indices.size() / 3U << " triangles, " << selector_list(hull);
        }
        out.detail = detail.str();
        return out;
    } catch (...) {
        return module_support::reject_minimal(probe);
    }
}

}  // namespace

NativeModule shw_module() noexcept {
    const auto caps = capability(ResourceCapability::Inspection) |
        ResourceCapability::Geometry |
        ResourceCapability::Wireframe |
        ResourceCapability::Adjacency |
        ResourceCapability::TransformSelectors;
    return {"formats.shw.hull-reader", "SHW", Format::Shw,
            ModuleKind::Mesh, true, run_shw_module, caps};
}

}  // namespace dmcresource
