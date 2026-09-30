#include "dmcresource/native_module.h"

#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <span>
#include <sstream>
#include <string>
#include <utility>

#include "dmcresource/environment_collision.h"
#include "dmcresource/module_support.h"

namespace dmcresource {
namespace {

PipelineResult run_hits_module(const NativeModule& module,
                               std::string_view filename,
                               const std::uint8_t* bytes,
                               std::size_t size,
                               const ProbeResult& probe) noexcept {
    try {
        const auto parsed = environment_collision::parse(
            filename, 0U,
            std::span<const std::uint8_t>{bytes, size});
        if (!parsed) {
            return module_support::reject(
                probe, module.id, "HITS rejected by canonical environment-collision parser");
        }

        PipelineResult out;
        out.accepted = true;
        // The triangle-plane records are drawn as a 3D surface (one flat
        // triangle each, lit by the record normal); the viewer outlines them.
        out.renderable = !parsed->triangles.empty();
        out.probe = probe;
        out.modules.push_back({"identity-probe", true});
        out.modules.push_back({"bounded-read-guard", true});
        out.modules.push_back({"canonical.hits.environment-collision-reader", true});
        out.modules.push_back({module.id, true});
        out.inspection.format = "HITS";
        out.inspection.root.id = "hits";
        out.inspection.root.title = "HITS environment collision";
        out.inspection.root.kind = InspectionKind::Document;
        out.inspection.root.source_span = SourceSpan{0U, size};
        const auto add = [&out](const char* key, std::string value, EvidenceLevel evidence) {
            out.inspection.root.properties.push_back({key, std::move(value), evidence});
        };
        add("Magic", "HITS", EvidenceLevel::ExeAndCorpusConfirmed);
        add("Evidence", parsed->evidence, EvidenceLevel::ExeAndCorpusConfirmed);
        add("Bounds", "min(" + std::to_string(parsed->bounds_min.x) + ", " +
                           std::to_string(parsed->bounds_min.y) + ", " +
                           std::to_string(parsed->bounds_min.z) + ") max(" +
                           std::to_string(parsed->bounds_max.x) + ", " +
                           std::to_string(parsed->bounds_max.y) + ", " +
                           std::to_string(parsed->bounds_max.z) + ")",
            EvidenceLevel::ExeConfirmed);
        add("Grid", std::to_string(parsed->grid_count_x) + " x " +
                     std::to_string(parsed->grid_count_y) + " x " +
                     std::to_string(parsed->grid_count_z), EvidenceLevel::ExeConfirmed);
        add("Cells", std::to_string(static_cast<std::uint64_t>(parsed->grid_count_x) *
                                     parsed->grid_count_y * parsed->grid_count_z),
            EvidenceLevel::ExeConfirmed);
        std::ostringstream cell_size;
        cell_size << "0x" << std::hex << std::uppercase << std::setfill('0')
                  << std::setw(8) << parsed->cell_size_raw[0] << " / 0x"
                  << std::setw(8) << parsed->cell_size_raw[1] << " / 0x"
                  << std::setw(8) << parsed->cell_size_raw[2];
        add("CellSizeRaw(+0x20)", cell_size.str(), EvidenceLevel::PreservedUndecoded);
        add("Triangle-plane records", std::to_string(parsed->triangles.size()),
            EvidenceLevel::ExeConfirmed);
        add("Cell references", std::to_string(parsed->cell_reference_count),
            EvidenceLevel::ExeConfirmed);
        add("Semantics", "raw flags and surface meanings preserved/undecoded",
            EvidenceLevel::PreservedUndecoded);

        if (!parsed->triangles.empty()) {
            MeshPrimitive primitive;
            primitive.name = "HITS triangle-plane records";
            auto& mesh = primitive.mesh;
            mesh.vertices.reserve(parsed->triangles.size() * 3U);
            mesh.normal0.reserve(parsed->triangles.size() * 3U);
            mesh.indices.reserve(parsed->triangles.size() * 3U);
            for (const auto& triangle : parsed->triangles) {
                for (const auto& point : {triangle.point_a, triangle.point_b, triangle.point_c}) {
                    mesh.indices.push_back(static_cast<std::uint32_t>(mesh.vertices.size()));
                    mesh.vertices.push_back(point);
                    mesh.normal0.push_back(triangle.normal);
                }
            }
            out.scene.meshes.push_back(std::move(primitive));
        }

        InspectionNode triangles;
        triangles.id = "triangles";
        triangles.title = "Triangle-plane records";
        triangles.kind = InspectionKind::Collection;
        triangles.properties.push_back({"Count", std::to_string(parsed->triangles.size()),
                                        EvidenceLevel::ExeConfirmed});
        out.inspection.root.children.push_back(std::move(triangles));

        std::ostringstream detail;
        detail << "HITS environment collision | grid=" << parsed->grid_count_x << 'x'
               << parsed->grid_count_y << 'x' << parsed->grid_count_z
               << " cells=" << static_cast<std::uint64_t>(parsed->grid_count_x) *
                                      parsed->grid_count_y * parsed->grid_count_z
               << " triangles=" << parsed->triangles.size()
               << " cellRefs=" << parsed->cell_reference_count
               << " | evidence=" << parsed->evidence;
        out.detail = detail.str();
        return out;
    } catch (...) {
        return module_support::reject_minimal(probe);
    }
}

}  // namespace

NativeModule hits_module() noexcept {
    return {"formats.hits.environment-collision-reader", "HITS", Format::Hits,
            ModuleKind::Mesh, true, run_hits_module,
            capability(ResourceCapability::Inspection) | ResourceCapability::Collision |
                ResourceCapability::Geometry | ResourceCapability::Wireframe};
}

}  // namespace dmcresource
