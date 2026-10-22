// Cutscene reader (demo.h, demo_session.h) on synthetic bytes: the .cam
// channels and their Hermite / linear keys, clip timing with its time scale,
// the archive layout (script, clip texts, name list / container pairs), the
// cutscene camera over the timeline and the viewer camera it becomes.
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

#include "dmcresource/demo.h"
#include "dmcresource/demo_session.h"
#include "dmcresource/dmc_resource.h"
#include "dmcresource/native_module.h"

using namespace dmcresource;

namespace {

using Bytes = std::vector<std::uint8_t>;

void put32(Bytes& b, std::size_t at, std::uint32_t v) { std::memcpy(b.data() + at, &v, 4U); }
void putf(Bytes& b, std::size_t at, float v) { std::memcpy(b.data() + at, &v, 4U); }
void push32(Bytes& b, std::uint32_t v) {
    b.resize(b.size() + 4U);
    put32(b, b.size() - 4U, v);
}
void pushf(Bytes& b, float v) {
    b.resize(b.size() + 4U);
    putf(b, b.size() - 4U, v);
}

bool near(float a, float b, float eps = 1.0e-3F) { return std::fabs(a - b) <= eps; }

// A camera over frames [start, end]: eye x Hermite (0 -> 100 with zero
// slopes), fov linear (40 -> 20), everything else from the base values.
Bytes make_cam(float start, float end, std::uint32_t first_key, std::uint32_t last_key) {
    Bytes b(0x60U, 0U);
    put32(b, 0x00U, 0x60U);
    std::memcpy(b.data() + 4U, "CAM\0", 4U);
    put32(b, 0x08U, (1U << 6U) | (1U << 12U));
    for (const auto [at, v] : {std::pair{0x10U, start}, {0x14U, end}, {0x18U, start}, {0x1CU, end}}) putf(b, at, v);
    const float base[14] = {10, 0, 0, 0, 0, 0, 5, 50, 7, 5, 50, 200, 45, 0};
    for (std::size_t i = 0; i < 14; ++i) putf(b, 0x20U + i * 4U, base[i]);
    push32(b, 2U);  // tracks
    // eye x: Hermite keys (frame | 0x8000, value, in, out)
    b.push_back(8U + 2U * 16U); b.push_back(0U); b.push_back(2U); b.push_back(0U);
    push32(b, 0xFFAA0001U);  // kind 1 (upper bytes are noise in the retail files)
    push32(b, first_key | 0x8000U); pushf(b, 0.0F); pushf(b, 0.0F); pushf(b, 0.0F);
    push32(b, last_key | 0x8000U); pushf(b, 100.0F); pushf(b, 0.0F); pushf(b, 0.0F);
    // fov: linear keys (frame, value)
    b.push_back(8U + 2U * 8U); b.push_back(0U); b.push_back(2U); b.push_back(0U);
    push32(b, 0U);
    push32(b, first_key); pushf(b, 40.0F);
    push32(b, last_key); pushf(b, 20.0F);
    return b;
}

Bytes text(std::string_view s) { return Bytes(s.begin(), s.end()); }

// PAC of slots, 16-byte aligned.
Bytes pac(const std::vector<Bytes>& slots) {
    const auto align = [](std::size_t v) { return (v + 15U) & ~std::size_t{15U}; };
    Bytes out(align(8U + slots.size() * 4U), 0U);
    std::memcpy(out.data(), "PAC\0", 4U);
    put32(out, 4U, static_cast<std::uint32_t>(slots.size()));
    for (std::size_t i = 0; i < slots.size(); ++i) {
        put32(out, 8U + i * 4U, static_cast<std::uint32_t>(out.size()));
        out.insert(out.end(), slots[i].begin(), slots[i].end());
        out.resize(align(out.size()), 0U);
    }
    return out;
}

}  // namespace

int main() {
    // ---- .cam ----
    const auto cam_bytes = make_cam(100.0F, 200.0F, 100U, 200U);
    assert(demo::looks_like_cam(cam_bytes));
    const auto cam = demo::parse_cam(cam_bytes);
    assert(cam && cam->tracks.size() == 2U && cam->start == 100.0F && cam->end == 200.0F);
    assert(cam->tracks[0].channel == demo::CamChannel::EyeX && cam->tracks[0].hermite);
    assert(cam->tracks[1].channel == demo::CamChannel::Fov && !cam->tracks[1].hermite);
    assert(cam->tracks[0].keys[0].frame == 100.0F);  // the 0x8000 flag is not part of the frame
    // Hermite with zero slopes: half way is half the value; the group
    // translate (10, 0, 0) moves eye and target.
    auto pose = demo::evaluate(*cam, 150.0F);
    assert(near(pose.eye.x, 10.0F + 50.0F) && near(pose.eye.y, 50.0F) && near(pose.eye.z, 7.0F));
    assert(near(pose.target.x, 15.0F) && near(pose.target.z, 200.0F));
    assert(near(pose.fov_degrees, 30.0F));
    // Ease: a quarter of the way is below a quarter of the value.
    assert(demo::evaluate(*cam, 125.0F).eye.x < 10.0F + 25.0F);
    // Held before the first and after the last key.
    assert(near(demo::evaluate(*cam, 0.0F).eye.x, 10.0F) && near(demo::evaluate(*cam, 999.0F).eye.x, 110.0F));
    // A negative range start: stored frames count from it.
    const auto early = demo::parse_cam(make_cam(-70.0F, 30.0F, 0U, 100U));
    assert(early && early->tracks[0].keys.front().frame == -70.0F && early->tracks[0].keys.back().frame == 30.0F);
    // The stage configuration's CAM member (tag at +0) is something else.
    Bytes stage_cam(64U, 0U);
    std::memcpy(stage_cam.data(), "CAM\0", 4U);
    assert(!demo::looks_like_cam(stage_cam));

    // ---- .hid: flagged objects and their states ----
    {
        Bytes hid(0x20U, 0U);
        put32(hid, 0x00U, 0x20U);
        std::memcpy(hid.data() + 4U, "HID\0", 4U);
        putf(hid, 0x0CU, 250.0F);
        putf(hid, 0x10U, 432.0F);
        hid[0x1CU] = 3U;          // three objects
        hid[0x1DU] = 1U;          // object 0 has a track
        hid[0x1FU] = 1U;          // object 2 has a track
        push32(hid, 2U);
        for (const float state : {-1.0F, 0.0F}) {
            hid.push_back(20U); hid.push_back(0U); hid.push_back(1U); hid.push_back(0U);
            push32(hid, 7U);
            pushf(hid, state);
            pushf(hid, 1.0F);
            hid.push_back(0x00U); hid.push_back(0xFAU); hid.push_back(0U); hid.push_back(0U);  // frame 250 (big-endian)
        }
        const auto parsed = demo::parse_hid(hid);
        assert(parsed && parsed->start == 250.0F && parsed->objects.size() == 2U);
        assert(parsed->objects[0].object == 0U && parsed->objects[0].shown);
        assert(parsed->objects[1].object == 2U && !parsed->objects[1].shown);
        Bytes not_hid(0x20U, 0U);
        assert(!demo::parse_hid(not_hid));
    }

    // ---- clips ----
    const std::string camera_set =
        "; camera clips\r\n# Clip\r\nSetFrame\t0.000000\r\nSkipFrame\t0.000000\r\nCutFrame\t0.000000\r\n"
        "ClipScale\t1.000000\r\nLife\t100.000000\r\nCamera\tcm1.cam\r\n# End\r\n"
        "# Clip\r\nSetFrame\t150.000000\r\nSkipFrame\t0.000000\r\nCutFrame\t0.000000\r\n"
        "ClipScale\t2.000000\r\nLife\t50.000000\r\nCamera\tcm1.cam\r\n# End\r\n# End\r\n";
    assert(demo::looks_like_clips(camera_set));
    const auto clips = demo::parse_clips(camera_set);
    assert(clips.size() == 2U && clips[1].clip_scale == 2.0F && clips[1].end_frame() == 250.0F);
    assert(clips[1].covers(200.0F) && !clips[1].covers(250.0F) && near(clips[1].local_frame(200.0F), 25.0F));
    assert(clips[0].value("Camera") == "cm1.cam" && clips[0].value("Nothing").empty());

    // ---- the archive ----
    const std::string script =
        "; (# = section)\r\n\r\n# Load\r\nCamera\t\tm99_t00_CameraLoad.txt\r\n\r\n# Set\r\n"
        "Camera\t\tm99_t00_CameraSet.txt\r\nModel\t00\tm99_t00_Model00Set.txt\r\n# End\r\n";
    const auto archive = pac({text(script), text("m99_t00_CameraSet.txt\r\nm99_t00_Model00Set.txt\r\n# End\r\n"),
                              pac({text(camera_set), text("# End\r\n")}),
                              text(";skipped.scm\r\ncm1.cam\r\n# End\r\n"), pac({cam_bytes})});
    assert(demo::looks_like_demo(archive));
    const auto doc = demo::parse_demo(archive);
    assert(doc && doc->name == "m99_t00" && doc->lists.size() == 2U);
    assert(doc->list("Camera") != nullptr && doc->list("Camera")->clips.size() == 2U);
    assert(doc->list("Model", 0) != nullptr && doc->list("Model", 0)->clips.empty());
    assert(doc->members.size() == 1U && doc->members[0].name == "cm1.cam" && doc->camera("cm1.cam") != nullptr);
    assert(doc->skipped.size() == 1U && doc->skipped[0] == "skipped.scm");
    assert(doc->end_frame == 250.0F);
    // The camera: inside the first clip (its own frames from the range start),
    // held after it, then the second clip at half speed.
    auto shot = demo::camera_at(*doc, 50.0F);
    assert(shot && shot->clip == 0 && near(shot->local_frame, 150.0F) && near(shot->pose.eye.x, 60.0F));
    shot = demo::camera_at(*doc, 120.0F);
    assert(shot && shot->clip == 0 && near(shot->local_frame, 200.0F));
    shot = demo::camera_at(*doc, 200.0F);
    assert(shot && shot->clip == 1 && near(shot->local_frame, 125.0F));

    // ---- identity ----
    assert(probe("m99_t00.pac", archive.data(), archive.size()).format == Format::Demo);
    assert(probe("x.bin", cam_bytes.data(), cam_bytes.size()).format == Format::DemoCamera);
    assert(probe("x.txt", reinterpret_cast<const std::uint8_t*>(camera_set.data()), camera_set.size()).format ==
           Format::DemoClips);
    for (const char* family : {"DEMO", "DEMO-CAM", "CLIPS"}) assert(NativeModuleRegistry::find(family) != nullptr);
    const auto info = run_decode_pipeline("cm1.cam", cam_bytes.data(), cam_bytes.size());
    assert(info.accepted && info.image_preview.width > 0 && info.inspection.root.children.size() == 3U);

    // ---- the viewer camera of a shot ----
    demo::CameraPose look;
    look.eye = {0.0F, 0.0F, 0.0F};
    look.target = {0.0F, 0.0F, 10.0F};
    look.fov_degrees = 2.0F * 0.5F * 180.0F / 3.14159265F;  // the viewer's own framing
    auto view = demo::view_of(look);
    assert(near(view.yaw, 0.0F) && near(view.pitch, 0.0F) && near(view.zoom, 1.0F));
    look.target = {10.0F, 10.0F, 0.0F};
    look.fov_degrees = 30.0F;
    view = demo::view_of(look);
    assert(near(view.yaw, 1.5707963F) && near(view.pitch, 0.7853982F) && view.zoom > 1.0F);

    // ---- opened as a scene: no models, no stage -> nothing to draw ----
    auto opened = open_session("m99_t00.pac", archive.data(), archive.size());
    assert(opened && !demo::is_demo(opened.get()) && opened->probe.format == Format::Demo);
    return 0;
}
