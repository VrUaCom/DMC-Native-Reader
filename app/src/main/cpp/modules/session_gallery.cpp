#include "dmcresource/resource_session.h"

#include <algorithm>
#include <cstdint>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "dmcresource/raster_card.h"

namespace dmcresource {
namespace {
bool valid_child(const Session* session, int index) noexcept {
    return index >= 0 && static_cast<std::size_t>(index) < session_child_count(session);
}
constexpr std::uint32_t kThumbnailSize = 256U;

ImagePreview square_thumbnail(const ImagePreview& source) {
    if (!source.available()) return {};

    ImagePreview out;
    out.width = kThumbnailSize;
    out.height = kThumbnailSize;
    out.rgba8.resize(static_cast<std::size_t>(kThumbnailSize) *
                     static_cast<std::size_t>(kThumbnailSize) * 4U);

    // Same dark neutral field as the gallery tile. A generated thumbnail is a
    // presentation surface only; the source image stays untouched and opens
    // at its real resolution when the tile is selected.
    for (std::size_t offset = 0U; offset < out.rgba8.size(); offset += 4U) {
        out.rgba8[offset + 0U] = 16U;
        out.rgba8[offset + 1U] = 16U;
        out.rgba8[offset + 2U] = 20U;
        out.rgba8[offset + 3U] = 255U;
    }

    std::uint32_t draw_w = kThumbnailSize;
    std::uint32_t draw_h = kThumbnailSize;
    if (source.width >= source.height) {
        draw_h = std::max<std::uint32_t>(
            1U,
            static_cast<std::uint32_t>(
                static_cast<std::uint64_t>(source.height) * kThumbnailSize /
                source.width));
    } else {
        draw_w = std::max<std::uint32_t>(
            1U,
            static_cast<std::uint32_t>(
                static_cast<std::uint64_t>(source.width) * kThumbnailSize /
                source.height));
    }

    const std::uint32_t origin_x = (kThumbnailSize - draw_w) / 2U;
    const std::uint32_t origin_y = (kThumbnailSize - draw_h) / 2U;
    for (std::uint32_t y = 0U; y < draw_h; ++y) {
        const auto source_y = std::min<std::uint32_t>(
            source.height - 1U,
            static_cast<std::uint32_t>(
                static_cast<std::uint64_t>(y) * source.height / draw_h));
        for (std::uint32_t x = 0U; x < draw_w; ++x) {
            const auto source_x = std::min<std::uint32_t>(
                source.width - 1U,
                static_cast<std::uint32_t>(
                    static_cast<std::uint64_t>(x) * source.width / draw_w));
            const auto source_offset =
                (static_cast<std::size_t>(source_y) * source.width + source_x) * 4U;
            const auto target_offset =
                (static_cast<std::size_t>(origin_y + y) * kThumbnailSize +
                 static_cast<std::size_t>(origin_x + x)) * 4U;
            std::copy_n(source.rgba8.data() + source_offset, 4U,
                        out.rgba8.data() + target_offset);
        }
    }
    return out;
}

// Draws `source` fitted (aspect kept) and centred in the cell, composited
// over the cell background.
void blit_fit(ImagePreview* out, std::uint32_t cell_x, std::uint32_t cell_y,
              std::uint32_t cell, const ImagePreview& source) {
    std::uint32_t draw_w = cell;
    std::uint32_t draw_h = cell;
    if (source.width >= source.height) {
        draw_h = std::max<std::uint32_t>(
            1U, static_cast<std::uint32_t>(
                    static_cast<std::uint64_t>(source.height) * cell / source.width));
    } else {
        draw_w = std::max<std::uint32_t>(
            1U, static_cast<std::uint32_t>(
                    static_cast<std::uint64_t>(source.width) * cell / source.height));
    }
    const auto ox = cell_x + (cell - draw_w) / 2U;
    const auto oy = cell_y + (cell - draw_h) / 2U;
    for (std::uint32_t y = 0U; y < draw_h; ++y) {
        const auto sy = std::min<std::uint32_t>(
            source.height - 1U,
            static_cast<std::uint32_t>(static_cast<std::uint64_t>(y) * source.height / draw_h));
        for (std::uint32_t x = 0U; x < draw_w; ++x) {
            const auto sx = std::min<std::uint32_t>(
                source.width - 1U,
                static_cast<std::uint32_t>(static_cast<std::uint64_t>(x) * source.width / draw_w));
            const auto* s = source.rgba8.data() +
                (static_cast<std::size_t>(sy) * source.width + sx) * 4U;
            auto* d = out->rgba8.data() +
                (static_cast<std::size_t>(oy + y) * out->width + ox + x) * 4U;
            const std::uint32_t a = s[3];
            for (std::size_t c = 0U; c < 3U; ++c) {
                d[c] = static_cast<std::uint8_t>((s[c] * a + d[c] * (255U - a) + 127U) / 255U);
            }
            d[3] = 255U;
        }
    }
}

// Gallery tile for a resource whose children are images (a PTX with its DDS
// textures, an effect bank, ...): one image fills the tile; two to four sit
// in a 2x2 grid (unused cells stay empty); five or more show three images
// and "+N" for the rest in the fourth cell.
ImagePreview image_mosaic(const Session& opened) {
    std::vector<const ImagePreview*> images;
    std::size_t image_children = 0U;
    for (const auto& child : opened.children) {
        const bool image = child.image_preview.available() ||
            has_capability(child.capabilities, ResourceCapability::ImagePreview);
        if (!image) continue;
        ++image_children;
        if (child.image_preview.available() && images.size() < 4U) {
            images.push_back(&child.image_preview);
        }
    }
    if (images.empty()) return {};

    raster::Canvas canvas(static_cast<int>(kThumbnailSize), static_cast<int>(kThumbnailSize));
    if (image_children == 1U) {
        auto out = canvas.take();
        blit_fit(&out, 0U, 0U, kThumbnailSize, *images.front());
        return out;
    }
    constexpr std::uint32_t kGap = 4U;
    constexpr std::uint32_t kCell = (kThumbnailSize - kGap) / 2U;
    const bool overflow = image_children > 4U;
    const std::size_t shown = std::min<std::size_t>(images.size(), overflow ? 3U : 4U);
    for (std::uint32_t i = 0U; i < 4U; ++i) {
        const auto x = (i % 2U) * (kCell + kGap);
        const auto y = (i / 2U) * (kCell + kGap);
        canvas.fill(static_cast<int>(x), static_cast<int>(y),
                    static_cast<int>(x + kCell), static_cast<int>(y + kCell), raster::kPanel);
    }
    if (overflow) {
        const auto label = "+" + std::to_string(image_children - 3U);
        constexpr int kScale = 5;
        const int cx = static_cast<int>(kCell + kGap + kCell / 2U);
        const int cy = static_cast<int>(kCell + kGap + kCell / 2U);
        canvas.text(cx - raster::Canvas::text_width(label, kScale) / 2, cy - 7 * kScale / 2,
                    label, raster::kLabel, kScale);
    }
    auto out = canvas.take();
    for (std::size_t i = 0U; i < shown; ++i) {
        const auto x = static_cast<std::uint32_t>(i % 2U) * (kCell + kGap);
        const auto y = static_cast<std::uint32_t>(i / 2U) * (kCell + kGap);
        blit_fit(&out, x, y, kCell, *images[i]);
    }
    return out;
}

ImagePreview square_thumbnail(RgbaImage image) {
    if (image.width <= 0 || image.height <= 0) return {};
    ImagePreview source{
        static_cast<std::uint32_t>(image.width),
        static_cast<std::uint32_t>(image.height),
        std::move(image.pixels),
    };
    return square_thumbnail(source);
}

const ImagePreview* materialize_child_thumbnail(
        const ChildResource& child, ImagePreview* scratch) {
    if (scratch == nullptr || child.source_bytes.empty()) return nullptr;

    auto opened = session_from_child(child);
    if (!opened) return nullptr;

    // Static 2D resources, MOT/CLT/TSC cards, raw effect records and the raw
    // binary fallback already expose ImagePreview through their ordinary
    // reader. Reuse that exact view rather than creating a gallery-only parser.
    if (opened->image_preview.available()) {
        *scratch = square_thumbnail(opened->image_preview);
        return scratch->available() ? scratch : nullptr;
    }

    // MOD / SCM / renderable EFM children have geometry rather than a static
    // image. Draw the same native scene the full viewer opens, at thumbnail
    // resolution. No format-specific Android path is introduced here.
    if (opened->renderable) {
        auto rendered = render_session(
            opened.get(),
            static_cast<int>(kThumbnailSize),
            static_cast<int>(kThumbnailSize),
            0.65F, -0.45F, 1.0F,
            render_flag(RenderFlag::Preview));
        *scratch = square_thumbnail(std::move(rendered));
        return scratch->available() ? scratch : nullptr;
    }

    // Children that are images (PTX textures, effect bank sprites): show the
    // images themselves rather than the evidence card.
    try {
        *scratch = image_mosaic(*opened);
        if (scratch->available()) return scratch;
    } catch (...) {
    }

    // A nested container may deliberately have children and therefore no
    // standalone image of its own. Its inspection document is still useful in
    // the gallery: show the same evidence card/byte surface the reader uses for
    // non-renderable files, while keeping the tile clickable for navigation.
    try {
        const auto bytes = std::span<const std::uint8_t>{
            child.source_bytes.data(), child.source_bytes.size()};
        *scratch = square_thumbnail(
            raster::render_info_card(opened->inspection, opened->detail, bytes));
        return scratch->available() ? scratch : nullptr;
    } catch (...) {
        return nullptr;
    }
}
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

    const auto& child = session->children[static_cast<std::size_t>(index)];
    if (child.image_preview.available()) {
        if (child.image_preview.width <= kThumbnailSize &&
            child.image_preview.height <= kThumbnailSize) {
            return {child.image_preview.width, child.image_preview.height};
        }
        return {kThumbnailSize, kThumbnailSize};
    }

    // The child has enough retained source bytes for the same native reader
    // that opens it on tap to create a thumbnail lazily. Returning a fixed
    // square here lets Android allocate one small bitmap instead of a full
    // 1080x1440 inspection card only to shrink it into a gallery tile.
    if (!child.source_bytes.empty()) return {kThumbnailSize, kThumbnailSize};
    return {};
}

const ImagePreview* session_child_preview(
    const Session* session, int index, ImagePreview* scratch) {
    if (!valid_child(session, index)) return nullptr;
    if (!session->uv_gallery) {
        const auto& child = session->children[static_cast<std::size_t>(index)];
        if (child.image_preview.available()) {
            // Preserve the original zero-copy transport for already-small
            // previews. Only large resident images are reduced for gallery
            // memory/bitmap cost.
            if (child.image_preview.width <= kThumbnailSize &&
                child.image_preview.height <= kThumbnailSize) {
                return &child.image_preview;
            }
            if (scratch == nullptr) return nullptr;
            *scratch = square_thumbnail(child.image_preview);
            return scratch->available() ? scratch : nullptr;
        }
        return materialize_child_thumbnail(child, scratch);
    }
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
