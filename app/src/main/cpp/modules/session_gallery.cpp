#include "dmcresource/resource_session.h"

namespace dmcresource {
namespace {
bool valid_child(const Session* session, int index) noexcept {
    return index >= 0 && static_cast<std::size_t>(index) < session_child_count(session);
}
constexpr std::uint32_t kThumbnailSize = 256U;
}

std::unique_ptr<Session> open_uv_gallery(const Session* model) {
    using namespace spider::black_widow;
    if (!has_state(black_widow_state(model), StateFlag::CanShowUv)) return nullptr;
    auto gallery = std::make_shared<UvGallery>(build_uv_gallery(
        model->render_mesh, model->render_triangle_texture_slots));
    if (gallery->maps.empty()) return nullptr;
    auto session = std::make_unique<Session>();
    session->uv_gallery = std::move(gallery);
    session->capabilities = capability(ResourceCapability::ChildResources) |
                            ResourceCapability::Inspection;
    session->detail = "UV maps grouped by canonical texture slot";
    return session;
}

std::size_t session_child_count(const Session* session) noexcept {
    if (!session) return 0;
    if (session->uv_gallery)
        return session->uv_map_index ? 0 : session->uv_gallery->maps.size();
    return session->children.size();
}

std::string session_child_title(const Session* session, int index) {
    if (!valid_child(session, index)) return {};
    if (!session->uv_gallery) return session->children[index].title;
    const auto& map = session->uv_gallery->maps[index];
    return "UV · Slot " + std::to_string(map.texture_slot) + " · " +
           std::to_string(map.indices.size() / 3U) + " triangles";
}

std::pair<std::uint32_t, std::uint32_t> session_child_preview_size(
    const Session* session, int index) noexcept {
    if (!valid_child(session, index)) return {};
    if (session->uv_gallery) return {kThumbnailSize, kThumbnailSize};
    const auto& preview = session->children[index].image_preview;
    if (!preview.available()) return {};
    return {preview.width, preview.height};
}

const ImagePreview* session_child_preview(
    const Session* session, int index, ImagePreview* scratch) {
    if (!valid_child(session, index)) return nullptr;
    if (!session->uv_gallery) return &session->children[index].image_preview;
    if (!scratch) return nullptr;
    const auto& gallery = *session->uv_gallery;
    auto image = render_uv_map(gallery.coordinates, gallery.maps[index].indices,
                               kThumbnailSize, kThumbnailSize, 1.0F);
    *scratch = {static_cast<std::uint32_t>(image.width),
                static_cast<std::uint32_t>(image.height), std::move(image.pixels)};
    return scratch;
}

std::unique_ptr<Session> open_session_child(const Session* parent, int index) {
    if (!valid_child(parent, index)) return nullptr;
    if (!parent->uv_gallery) return session_from_child(parent->children[index]);
    auto session = std::make_unique<Session>();
    session->uv_gallery = parent->uv_gallery;
    session->uv_map_index = static_cast<std::size_t>(index);
    session->capabilities = capability(ResourceCapability::Inspection);
    session->detail = session_child_title(parent, index);
    return session;
}
}  // namespace dmcresource
