#include "dmcresource/resource_session.h"
#include <cassert>
#include <algorithm>
#include <cstdlib>
#include <limits>
#include <utility>
#include "dmcresource/session_inspection.h"
#include "dmcresource/inspection_format.h"

using namespace dmcresource;
namespace widow = dmcresource::spider::black_widow;

int main() {
    auto model = std::make_unique<Session>();
    model->renderable = true;
    model->capabilities = ResourceCapability::Geometry | ResourceCapability::UvCoordinates |
                          ResourceCapability::TextureBinding | ResourceCapability::Inspection;
    // Two disjoint triangles plus a second primitive sharing slot 9. Sparse,
    // out-of-order slots must stay canonical rather than become gallery ordinals.
    model->render_mesh.vertices.resize(6);
    model->render_mesh.uv0 = {{.1F,.1F},{.4F,.1F},{.1F,.4F},
                             {.6F,.6F},{.9F,.6F},{.9F,.9F}};
    model->render_mesh.indices = {0,1,2,3,4,5,0,1,2};
    model->render_triangle_texture_slots = {9,2,9};
    auto gallery = open_uv_gallery(model.get());
    assert(gallery && session_child_count(gallery.get()) == 2);
    const auto& groups = gallery->uv_gallery->maps;
    assert(groups[0].texture_slot == 2 && groups[1].texture_slot == 9);
    assert(groups[0].indices == std::vector<std::uint32_t>({3,4,5}));
    assert(groups[1].indices == std::vector<std::uint32_t>({0,1,2,0,1,2}));
    assert(session_child_title(gallery.get(), 0).find("Slot 2") != std::string::npos);
    const auto gallery_state = black_widow_state(gallery.get());
    assert(widow::has_state(gallery_state, widow::StateFlag::ChildBrowserMode));
    // Device regression: the reset control must become the PNG-download control
    // for the whole UV gallery, not remain a disabled refresh button.
    assert(widow::has_state(gallery_state, widow::StateFlag::CanExportPng));
    assert(!widow::has_state(gallery_state, widow::StateFlag::CanShowUv));
    assert(!open_session_child(gallery.get(), -1));
    assert(!open_session_child(gallery.get(), 2));
    assert(session_child_preview_size(gallery.get(), -1).first == 0);
    auto map2 = open_session_child(gallery.get(), 0);
    auto map9 = open_session_child(gallery.get(), 1);
    assert(map2 && map9);
    assert(map2->uv_gallery == gallery->uv_gallery && map9->uv_gallery == gallery->uv_gallery);
    assert(map2->render_mesh.vertices.empty() && map2->scene.meshes.empty());
    assert(session_child_count(map2.get()) == 0);
    const auto state = black_widow_state(map2.get());
    assert(widow::has_state(state, widow::StateFlag::UvMapView));
    assert(widow::has_state(state, widow::StateFlag::CanRender));
    // Device regression: an opened UV map must expose single-image PNG export.
    assert(widow::has_state(state, widow::StateFlag::CanExportPng));
    assert(!widow::has_state(state, widow::StateFlag::CanWireframe));
    assert(!widow::has_state(state, widow::StateFlag::TextureCompanionAttachable));
    const auto image2 = render_session(map2.get(), 256,256,0,0,1,0);
    const auto image9 = render_session(map9.get(), 256,256,0,0,1,0);
    // Pixel witnesses on each triangle: a map must never include the other slot.
    // v runs down the image as the texture is sampled (row v * height), so the
    // v = 0.6 edge of slot 2 lies below the centre and v = 0.1 of slot 9 above.
    auto wire = [](const RgbaImage& im, int x, int y) {
        return im.pixels[(y * im.width + x) * 4] == 235;
    };
    assert(wire(image2, 170,150) && !wire(image9,170,150));
    assert(wire(image9, 60,42) && !wire(image2,60,42));
    auto coordinates = map2->uv_gallery->coordinates;
    coordinates[0] = {20.0F, 20.0F};
    assert(render_uv_map(coordinates, groups[0].indices,256,256,1).pixels == image2.pixels);
    ImagePreview scratch;
    const auto* thumb = session_child_preview(gallery.get(), 0, &scratch);
    assert(thumb == &scratch && thumb->rgba8 == image2.pixels);
    assert(render_session(map2.get(),256,256,0,0,2,0).pixels != image2.pixels);
    // Parent lifetime and mutations cannot invalidate open maps or create copies.
    model->render_mesh.uv0[0].u = 100;
    assert(render_session(map9.get(),256,256,0,0,1,0).pixels == image9.pixels);
    model.reset();
    gallery.reset();
    assert(render_session(map2.get(),256,256,0,0,1,0).pixels == image2.pixels);

    // Proportions come from the texture painted on the slot: 512 x 256 is 2:1.
    {
        auto textured = std::make_unique<Session>();
        textured->renderable = true;
        textured->capabilities = ResourceCapability::Geometry | ResourceCapability::UvCoordinates |
                                 ResourceCapability::TextureBinding | ResourceCapability::Inspection;
        textured->render_mesh.vertices.resize(3);
        textured->render_mesh.uv0 = {{0.0F, 0.0F}, {1.0F, 0.0F}, {0.0F, 1.0F}};
        textured->render_mesh.indices = {0, 1, 2};
        textured->render_triangle_texture_slots = {1};
        textured->attached_textures.resize(2);
        textured->attached_textures[1].width = 512;
        textured->attached_textures[1].height = 256;
        textured->attached_textures[1].rgba8.assign(512U * 256U * 4U, 0U);
        auto uv = open_uv_gallery(textured.get());
        assert(uv && uv->uv_gallery->maps.size() == 1U);
        const auto& map = uv->uv_gallery->maps[0];
        assert(map.texture_width == 512U && map.texture_height == 256U);
        assert(uv_map_proportion(map) == "2:1");
        assert(session_child_title(uv.get(), 0).find("2:1") != std::string::npos);
        // Gallery tile in the same proportions, longer side 256.
        const auto tile = session_child_preview_size(uv.get(), 0);
        assert(tile.first == 256U && tile.second == 128U);
        const auto* tile_image = session_child_preview(uv.get(), 0, &scratch);
        assert(tile_image && tile_image->width == 256U && tile_image->height == 128U);
        // PNG: the texture's resolution times a whole factor, 1024 x 512.
        auto opened = open_session_child(uv.get(), 0);
        assert(opened);
        assert(uv_map_export_size(opened.get()) == std::make_pair(1024, 512));
        const auto png = render_uv_map_export(opened.get());
        assert(png.width == 1024 && png.height == 512);
        // The texture rectangle fills the PNG and v runs down: the u = 0 edge
        // is the left column, the v = 0 edge the top row, the hypotenuse
        // passes through the centre.
        auto lit = [](const RgbaImage& im, int x, int y) {
            return im.pixels[(static_cast<std::size_t>(y) * im.width + x) * 4] == 235;
        };
        assert(lit(png, 0, 256) && lit(png, 512, 0) && lit(png, 512, 256));
        assert(!lit(png, 900, 450));
        // Viewing keeps the 2:1 rectangle in a square viewport.
        const auto view = render_session(opened.get(), 256, 256, 0, 0, 1, 0);
        int left = 256, right = -1, top = 256, bottom = -1;
        for (int y = 0; y < 256; ++y) {
            for (int x = 0; x < 256; ++x) {
                if (view.pixels[(static_cast<std::size_t>(y) * 256U + x) * 4U] < 60U) continue;
                left = std::min(left, x); right = std::max(right, x);
                top = std::min(top, y); bottom = std::max(bottom, y);
            }
        }
        const int drawn_w = right - left, drawn_h = bottom - top;
        assert(drawn_w > 0 && drawn_h > 0 && std::abs(drawn_w - 2 * drawn_h) <= 2);
        // Info names the texture and the proportions.
        const auto info = format_inspection_tree(inspect_session(opened.get(), InspectionTopic::Uv));
        assert(info.find("512 x 256") != std::string::npos && info.find("2:1") != std::string::npos);
    }
    // Unknown texture: drawn square, said so; a huge texture is capped at 4096.
    {
        UvMap unknown;
        assert(uv_map_proportion(unknown) == "1:1 (no texture)");
        assert(uv_map_export_size(unknown) == std::make_pair(1024, 1024));
        UvMap tall{.texture_width = 96U, .texture_height = 128U};
        assert(uv_map_proportion(tall) == "3:4");
        assert(uv_map_export_size(tall) == std::make_pair(768, 1024));
        UvMap huge{.texture_width = 8192U, .texture_height = 2048U};
        assert(uv_map_export_size(huge) == std::make_pair(4096, 1024));
    }

    Mesh malformed;
    malformed.vertices.resize(3);
    malformed.uv0 = {{0,0},{1,0},{0,1}};
    malformed.indices = {0,1,2};
    assert(build_uv_gallery(malformed, {}).maps.empty());
    assert(build_uv_gallery(malformed, std::vector<std::uint32_t>{kNoTextureSlot}).maps.empty());
    malformed.uv0[0].u = std::numeric_limits<float>::quiet_NaN();
    assert(build_uv_gallery(malformed, std::vector<std::uint32_t>{0}).maps.empty());

    // Shared gallery transport borrows existing PTX previews without RGBA copies.
    Session ptx;
    ChildResource child;
    child.title = "Texture 7";
    child.image_preview = {1,1,{255,0,0,255}};
    child.capabilities = capability(ResourceCapability::ImagePreview);
    ptx.children.push_back(std::move(child));
    assert(session_child_count(&ptx) == 1);
    assert(session_child_preview(&ptx,0,&scratch) == &ptx.children[0].image_preview);
    auto image_child = open_session_child(&ptx,0);
    assert(image_child && image_child->image_preview.available());
    assert(!open_uv_gallery(image_child.get()));
    return 0;
}
