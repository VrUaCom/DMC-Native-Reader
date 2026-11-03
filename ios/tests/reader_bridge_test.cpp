// Host regression for the iOS shell's C++ boundary (ios/DMCReader/Bridge).
//
// It drives the exact translation unit the iOS app compiles, against the real
// DMCNativeReader::Core, using fixtures borrowed from the canonical Core
// regressions. This is deliberately NOT part of the canonical native test
// inventory in app/src/main/cpp/CMakeLists.txt, whose exact size is a Phase-2
// evidence contract; it is built from ios/tests/CMakeLists.txt instead.

#include "reader_bridge.h"

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

std::vector<std::uint8_t> bridge_fixture_scm();
std::vector<std::uint8_t> bridge_fixture_mod();
std::vector<std::uint8_t> bridge_fixture_dds();
std::vector<std::uint8_t> bridge_fixture_ptx();

namespace {

int failures = 0;

void check(bool condition, const char* what) {
    std::printf("  %s %s\n", condition ? "ok  " : "FAIL", what);
    if (!condition) ++failures;
}

bool has(std::uint64_t bits, std::uint64_t flag) { return (bits & flag) != 0U; }

bool opaque_pixels(const dmc_ios::Pixels& pixels) {
    if (pixels.rgba.size() !=
        static_cast<std::size_t>(pixels.width) * pixels.height * 4U) {
        return false;
    }
    for (std::size_t i = 3; i < pixels.rgba.size(); i += 4) {
        if (pixels.rgba[i] == 0U) return false;
    }
    return true;
}

void test_scm_model() {
    std::printf("SCM model\n");
    const auto bytes = bridge_fixture_scm();
    auto session = dmc_ios::ReaderSession::open("sample.scm", bytes.data(), bytes.size());
    check(session != nullptr, "opens");
    if (!session) return;

    const auto bits = session->state();
    check(has(bits, dmc_ios::state::kCanRender), "Black Widow: can render");
    check(has(bits, dmc_ios::state::kCanWireframe), "Black Widow: can wireframe");
    check(!has(bits, dmc_ios::state::kChildBrowserMode), "not a child browser");
    check(!session->describe().empty(), "describe() is populated");
    check(session->inspection().find("SCM") != std::string::npos,
          "inspection names the format");

    dmc_ios::Pixels shaded;
    check(session->render(96, 64, 0.65F, -0.45F, 1.0F, 0U, &shaded), "renders");
    check(shaded.width == 96U && shaded.height == 64U, "honours requested size");
    check(opaque_pixels(shaded), "pixel buffer is complete and opaque");

    dmc_ios::Pixels wire;
    check(session->render(96, 64, 0.65F, -0.45F, 1.0F,
                          dmc_ios::render_flag::kWireframe, &wire),
          "renders wireframe");
    check(wire.rgba != shaded.rgba, "wireframe output differs from shaded");

    dmc_ios::Pixels none;
    check(!session->image_preview(&none), "a model has no image preview");
    check(session->child_count() == 0U, "a model has no children");
    check(session->open_child(0) == nullptr, "open_child out of range is rejected");
    check(!session->render(96, 64, 0.0F, 0.0F, 1.0F, 0U, nullptr),
          "render into null output is rejected");
}

void test_texture_attach() {
    std::printf("texture companion on SCM\n");
    const auto scm = bridge_fixture_scm();
    auto session = dmc_ios::ReaderSession::open("sample.scm", scm.data(), scm.size());
    check(session != nullptr, "opens");
    if (!session) return;
    check(has(session->state(), dmc_ios::state::kTextureCompanionAttachable),
          "Black Widow: texture companion attachable");

    check(!session->attach_texture("bad.ptx", nullptr, 0U), "empty companion rejected");

    const auto ptx = bridge_fixture_ptx();
    const bool attached = session->attach_texture("sample.ptx", ptx.data(), ptx.size());
    check(!session->texture_attachment_detail().empty(),
          "attachment outcome is reported");
    check(attached == has(session->state(), dmc_ios::state::kTextureCompanionAttached),
          "attach result agrees with Black Widow state");
    std::printf("       attach=%d detail=\"%s\"\n", attached ? 1 : 0,
                session->texture_attachment_detail().c_str());
}

void test_mod_model() {
    std::printf("MOD model\n");
    const auto bytes = bridge_fixture_mod();
    auto session = dmc_ios::ReaderSession::open("sample.mod", bytes.data(), bytes.size());
    check(session != nullptr, "opens");
    if (!session) return;
    dmc_ios::Pixels image;
    check(session->render(64, 64, 0.2F, 0.1F, 1.5F, 0U, &image), "renders");
}

void test_dds_image() {
    std::printf("DDS image\n");
    const auto bytes = bridge_fixture_dds();
    auto session = dmc_ios::ReaderSession::open("sample.dds", bytes.data(), bytes.size());
    check(session != nullptr, "opens");
    if (!session) return;
    check(has(session->state(), dmc_ios::state::kCanPreviewImage),
          "Black Widow: image preview");
    dmc_ios::Pixels image;
    check(session->image_preview(&image), "image preview decodes");
    check(image.width > 0U && image.height > 0U &&
              image.rgba.size() == static_cast<std::size_t>(image.width) * image.height * 4U,
          "preview buffer is consistent");
}

void test_ptx_bundle() {
    std::printf("PTX bundle\n");
    const auto bytes = bridge_fixture_ptx();
    auto session = dmc_ios::ReaderSession::open("sample.ptx", bytes.data(), bytes.size());
    check(session != nullptr, "opens");
    if (!session) return;
    check(has(session->state(), dmc_ios::state::kChildBrowserMode),
          "Black Widow: child browser mode");
    check(session->child_count() == 1U, "publishes one child");
    check(session->child_title(0) == "DDS 0", "child title");
    check(session->child_title(7).empty(), "out-of-range child title is empty");

    dmc_ios::Pixels thumb;
    check(session->child_preview(0, &thumb), "child preview decodes");
    check(!session->child_preview(7, &thumb), "out-of-range child preview is rejected");

    auto child = session->open_child(0);
    check(child != nullptr, "child opens as its own session");
    if (child) {
        dmc_ios::Pixels image;
        check(child->image_preview(&image), "child session has an image preview");
    }
    session.reset();
    check(child != nullptr && !child->describe().empty(),
          "child outlives its parent session");
}

void test_rejections() {
    std::printf("rejections\n");
    const std::vector<std::uint8_t> junk{'J', 'U', 'N', 'K', 0, 1, 2, 3};
    check(dmc_ios::ReaderSession::open("junk.mod", junk.data(), junk.size()) == nullptr,
          "foreign bytes under a known extension are rejected");
    check(dmc_ios::ReaderSession::open("empty.scm", nullptr, 0U) == nullptr,
          "empty input is rejected");
    check(dmc_ios::ReaderSession::open("bad.scm", nullptr, 16U) == nullptr,
          "null bytes with a size are rejected");
}

}  // namespace

int main() {
    test_scm_model();
    test_texture_attach();
    test_mod_model();
    test_dds_image();
    test_ptx_bundle();
    test_rejections();
    if (failures != 0) {
        std::printf("\n%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("\nall iOS bridge checks passed\n");
    return 0;
}
