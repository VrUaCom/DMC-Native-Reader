#include "dmcresource/composite_placement.h"

#include <limits>
#include <new>
#include <utility>
#include <vector>

#include "dmcresource/matrix_ops.h"
#include "dmcresource/resource_session.h"
#include "dmcresource/scene_projection.h"

namespace dmcresource::composite_placement {
namespace {

[[nodiscard]] bool add_size(std::size_t value,
                            std::size_t add,
                            std::size_t* out) noexcept {
    if (out == nullptr || value > std::numeric_limits<std::size_t>::max() - add) {
        return false;
    }
    *out = value + add;
    return true;
}

[[nodiscard]] bool source_vertex_count(const CompositePart& part,
                                       std::size_t* out) noexcept {
    if (out == nullptr) return false;
    std::size_t total = 0U;
    for (const auto& primitive : part.scene.meshes) {
        if (!add_size(total, primitive.mesh.vertices.size(), &total)) return false;
    }
    *out = total;
    return true;
}

[[nodiscard]] bool part_vertex_range(const Session& session,
                                     std::size_t target,
                                     std::size_t* begin,
                                     std::size_t* count) noexcept {
    if (begin == nullptr || count == nullptr || target >= session.composite_parts.size()) {
        return false;
    }
    std::size_t cursor = 0U;
    for (std::size_t index = 0U; index < session.composite_parts.size(); ++index) {
        std::size_t local_count = 0U;
        if (!source_vertex_count(session.composite_parts[index], &local_count)) return false;
        if (index == target) {
            *begin = cursor;
            *count = local_count;
        }
        if (!add_size(cursor, local_count, &cursor)) return false;
    }
    return cursor == session.render_mesh.vertices.size() &&
           *begin <= session.render_mesh.vertices.size() &&
           *count <= session.render_mesh.vertices.size() - *begin;
}

[[nodiscard]] bool part_node_range(const Session& session,
                                   std::size_t target,
                                   std::size_t* begin,
                                   std::size_t* count) noexcept {
    if (begin == nullptr || count == nullptr || target >= session.composite_parts.size()) {
        return false;
    }
    std::size_t cursor = 0U;
    for (std::size_t index = 0U; index < session.composite_parts.size(); ++index) {
        const auto local_count = session.composite_parts[index].scene.nodes.size();
        if (index == target) {
            *begin = cursor;
            *count = local_count;
        }
        if (!add_size(cursor, local_count, &cursor)) return false;
    }
    return cursor == session.scene.nodes.size() &&
           *begin <= session.scene.nodes.size() &&
           *count <= session.scene.nodes.size() - *begin;
}

[[nodiscard]] PlacementResult apply_projection(
    Session* session,
    std::size_t child_part_index,
    const Matrix4& root_matrix,
    CompositePlacementMode mode,
    InstanceId host_instance_id,
    std::size_t host_part_index,
    std::uint32_t selector,
    PlacementStatus success_status) noexcept {
    if (session == nullptr || session->composite_parts.empty()) {
        return {.status = PlacementStatus::InvalidSession};
    }
    if (child_part_index >= session->composite_parts.size()) {
        return {.status = PlacementStatus::InvalidPartIndex};
    }
    if (!matrix_ops::is_finite_affine(root_matrix)) {
        return {.status = PlacementStatus::MatrixRejected};
    }

    try {
        auto& part = session->composite_parts[child_part_index];
        Mesh source_mesh;
        if (!materialize_render_scene(part.scene, &source_mesh)) {
            return {.status = PlacementStatus::SourceProjectionFailed};
        }

        std::size_t vertex_begin = 0U;
        std::size_t vertex_count = 0U;
        if (!part_vertex_range(*session, child_part_index, &vertex_begin, &vertex_count) ||
            source_mesh.vertices.size() != vertex_count) {
            return {.status = PlacementStatus::CompositeProjectionMismatch};
        }

        std::size_t node_begin = 0U;
        std::size_t node_count = 0U;
        if (!part_node_range(*session, child_part_index, &node_begin, &node_count) ||
            part.scene.nodes.size() != node_count) {
            return {.status = PlacementStatus::CompositeProjectionMismatch};
        }

        std::vector<Vec3> staged_vertices;
        staged_vertices.reserve(source_mesh.vertices.size());
        for (const auto& vertex : source_mesh.vertices) {
            Vec3 placed;
            if (!matrix_ops::transform_point(vertex, root_matrix, &placed)) {
                return {.status = PlacementStatus::MatrixRejected};
            }
            staged_vertices.push_back(placed);
        }

        std::vector<Matrix4> staged_world;
        staged_world.reserve(part.scene.nodes.size());
        for (const auto& node : part.scene.nodes) {
            Matrix4 placed{};
            if (!matrix_ops::multiply(node.world, root_matrix, &placed)) {
                return {.status = PlacementStatus::MatrixRejected};
            }
            staged_world.push_back(placed);
        }

        const auto old_placement = part.placement;
        const auto old_overlay = session->hierarchy_overlay;
        std::vector<Vec3> old_vertices(
            session->render_mesh.vertices.begin() + static_cast<std::ptrdiff_t>(vertex_begin),
            session->render_mesh.vertices.begin() +
                static_cast<std::ptrdiff_t>(vertex_begin + vertex_count));
        std::vector<Matrix4> old_world;
        old_world.reserve(node_count);
        for (std::size_t index = 0U; index < node_count; ++index) {
            old_world.push_back(session->scene.nodes[node_begin + index].world);
        }

        for (std::size_t index = 0U; index < vertex_count; ++index) {
            session->render_mesh.vertices[vertex_begin + index] = staged_vertices[index];
        }
        for (std::size_t index = 0U; index < node_count; ++index) {
            session->scene.nodes[node_begin + index].world = staged_world[index];
        }

        part.placement.mode = mode;
        part.placement.host_instance_id = host_instance_id;
        part.placement.host_part_index = host_part_index;
        part.placement.attachment_selector = selector;
        part.placement.root_matrix = root_matrix;
        part.placement.resolved = mode != CompositePlacementMode::SourceCoordinates;

        HierarchyOverlay staged_overlay;
        if (!materialize_hierarchy_overlay(session->scene, &staged_overlay)) {
            for (std::size_t index = 0U; index < vertex_count; ++index) {
                session->render_mesh.vertices[vertex_begin + index] = old_vertices[index];
            }
            for (std::size_t index = 0U; index < node_count; ++index) {
                session->scene.nodes[node_begin + index].world = old_world[index];
            }
            part.placement = old_placement;
            session->hierarchy_overlay = old_overlay;
            return {.status = PlacementStatus::CompositeProjectionMismatch};
        }
        session->hierarchy_overlay = std::move(staged_overlay);
        return {.status = success_status, .root_matrix = root_matrix};
    } catch (const std::bad_alloc&) {
        return {.status = PlacementStatus::AllocationFailed};
    } catch (...) {
        return {.status = PlacementStatus::CompositeProjectionMismatch};
    }
}

}  // namespace

PlacementResult attach_to_host_joint(Session* session,
                                     std::size_t host_part_index,
                                     std::size_t child_part_index,
                                     std::uint32_t host_joint_index) noexcept {
    if (session == nullptr || session->composite_parts.empty()) {
        return {.status = PlacementStatus::InvalidSession};
    }
    if (host_part_index >= session->composite_parts.size() ||
        child_part_index >= session->composite_parts.size()) {
        return {.status = PlacementStatus::InvalidPartIndex};
    }
    if (host_part_index == child_part_index) {
        return {.status = PlacementStatus::SamePart};
    }

    const auto& host = session->composite_parts[host_part_index];
    if (host.instance_id == kInvalidInstanceId ||
        session->workspace_graph.find_instance(host.instance_id) == nullptr) {
        return {.status = PlacementStatus::InvalidSession};
    }
    if (host_joint_index >= host.scene.nodes.size()) {
        return {.status = PlacementStatus::HostJointUnavailable};
    }

    std::size_t host_node_begin = 0U;
    std::size_t host_node_count = 0U;
    if (!part_node_range(*session, host_part_index, &host_node_begin, &host_node_count) ||
        host_joint_index >= host_node_count) {
        return {.status = PlacementStatus::CompositeProjectionMismatch};
    }

    const auto& host_joint =
        session->scene.nodes[host_node_begin + static_cast<std::size_t>(host_joint_index)];
    if (!host_joint.spatial_authority) {
        return {.status = PlacementStatus::HostJointWithoutSpatialAuthority};
    }
    if (!matrix_ops::is_finite_affine(host_joint.world)) {
        return {.status = PlacementStatus::MatrixRejected};
    }

    return apply_projection(
        session,
        child_part_index,
        host_joint.world,
        CompositePlacementMode::HostJoint,
        host.instance_id,
        host_part_index,
        host_joint_index,
        PlacementStatus::Applied);
}

PlacementResult reset_to_source_coordinates(Session* session,
                                            std::size_t child_part_index) noexcept {
    Matrix4 identity{};
    return apply_projection(
        session,
        child_part_index,
        identity,
        CompositePlacementMode::SourceCoordinates,
        kInvalidInstanceId,
        kNoCompositePart,
        kNoAttachmentSelector,
        PlacementStatus::Reset);
}

}  // namespace dmcresource::composite_placement
