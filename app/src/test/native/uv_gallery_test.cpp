#include "dmcresource/resource_session.h"
#include <cassert>
#include <limits>

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
    assert(widow::has_state(black_widow_state(gallery.get()), widow::StateFlag::ChildBrowserMode));
    assert(!widow::has_state(black_widow_state(gallery.get()), widow::StateFlag::CanShowUv));
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
    assert(!widow::has_state(state, widow::StateFlag::CanWireframe));
    assert(!widow::has_state(state, widow::StateFlag::TextureCompanionAttachable));
    const auto image2 = render_session(map2.get(), 256,256,0,0,1,0);
    const auto image9 = render_session(map9.get(), 256,256,0,0,1,0);
    // Pixel witnesses on each triangle: a map must never include the other slot.
    auto wire = [](const RgbaImage& im, int x, int y) {
        return im.pixels[(y * im.width + x) * 4] == 235;
    };
    assert(wire(image2, 170,106) && !wire(image9,170,106));
    assert(wire(image9, 60,214) && !wire(image2,60,214));
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
