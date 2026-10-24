// The reader's photo and video camera (spider/camera.h): the settings and
// their starting values, the sizes of pictures and videos, the framing that
// keeps a capture equal to the viewfinder, the exposure and the developing.
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <vector>

#include <fcntl.h>
#include <unistd.h>

#include "dmcresource/spider/camera.h"

using namespace dmcresource;
namespace cam = dmcresource::spider::camera;

namespace {

int index_of(cam::Key key) {
    const auto& list = cam::settings();
    for (std::size_t i = 0; i < list.size(); ++i) {
        if (list[i].key == key) return static_cast<int>(i);
    }
    return -1;
}

void choose(cam::Key key, int option) {
    std::vector<int> values;
    for (const auto& s : cam::settings()) values.push_back(cam::choice(s.key));
    values[static_cast<std::size_t>(index_of(key))] = option;
    cam::set_choices(values);
}

RgbaImage grey(int w, int h, std::uint8_t v) {
    RgbaImage image;
    image.width = w;
    image.height = h;
    image.pixels.assign(static_cast<std::size_t>(w) * h * 4U, v);
    for (std::size_t i = 3; i < image.pixels.size(); i += 4) image.pixels[i] = 255U;
    return image;
}

}  // namespace

int main() {
    // Every setting is listed once, with options and a starting value in range.
    const auto& list = cam::settings();
    assert(list.size() == static_cast<std::size_t>(cam::Key::Count));
    for (std::size_t i = 0; i < list.size(); ++i) {
        assert(static_cast<std::size_t>(list[i].key) == i);
        assert(!list[i].id.empty() && !list[i].section.empty() && list[i].options.size() >= 2U);
        assert(list[i].start >= 0 && list[i].start < static_cast<int>(list[i].options.size()));
        assert(cam::choice(list[i].key) == list[i].start);
    }

    // Starting values: a PNG of the view's frame, a 1080p 16:9 H.264 at 30.
    auto photo = cam::plan(cam::Kind::Photo, 2400, 1080);
    assert(photo.width == 2400 && photo.height == 1080 && photo.format == 0 && photo.burst == 1);
    assert(photo.subtitles && photo.timer_seconds == 0);
    auto video = cam::plan(cam::Kind::Video, 2400, 1080);
    assert(video.width == 1920 && video.height == 1072 && video.fps == 30 && video.format == 0);
    assert(video.keyframe_ms == 1000 && video.limit_seconds == 0 && video.quality > 1'500'000);
    // The video's frame in a 20:9 view is 16:9 with bars at the sides.
    const auto frame = cam::frame_in_view(cam::Kind::Video, 2400, 1080);
    assert(std::abs(frame.height - 1080.0F) < 0.5F && std::abs(frame.width - 1920.0F) < 0.5F);
    assert(std::abs(frame.x - 240.0F) < 0.5F && frame.y == 0.0F);

    // 8K and 9:16: the long side is 7680, upright.
    choose(cam::Key::PhotoSize, 7);
    choose(cam::Key::PhotoAspect, 6);
    photo = cam::plan(cam::Kind::Photo, 2400, 1080);
    assert(photo.height == 7680 && photo.width == 4320);
    // An upright frame in a wide view: the capture zooms so it shows what
    // the viewfinder frame shows (the view's short side over the frame's).
    const auto upright = cam::frame_in_view(cam::Kind::Photo, 2400, 1080);
    assert(std::abs(upright.width - 607.5F) < 0.5F);
    assert(std::abs(cam::capture_zoom(cam::Kind::Photo, 1.0F, 2400, 1080) - 1080.0F / 607.5F) < 1e-3F);
    cam::reset_choices();
    assert(std::abs(cam::capture_zoom(cam::Kind::Photo, 1.3F, 2400, 1080) - 1.3F) < 1e-5F);

    // A fixed lens: 50 mm frames 24 mm of short side, whatever the view zoom.
    choose(cam::Key::LensFocal, 6);
    assert(std::abs(cam::focal_mm(cam::capture_zoom(cam::Kind::Photo, 3.0F, 2400, 1080)) - 50.0F) < 0.01F);
    assert(std::abs(cam::focal_mm(1.0F) - 21.97F) < 0.05F);  // the view's zoom 1 is about 22 mm
    cam::reset_choices();

    // Exposure: auto keeps the rendered picture; manual counts the light.
    assert(cam::exposure().gain == 1.0F && cam::exposure().iso == 100);
    choose(cam::Key::ExposureMode, 1);
    choose(cam::Key::ExposureIso, 4);       // ISO 400
    auto e = cam::exposure();
    assert(e.iso == 400 && std::abs(e.gain - 4.0F) < 1e-4F);  // two stops over at 1/60 f/4
    choose(cam::Key::ExposureIso, 0);       // auto ISO
    choose(cam::Key::ExposureShutter, 5);   // 1/250
    e = cam::exposure();
    assert(e.iso > 400 && e.iso < 420 && std::abs(e.gain - 1.0F) < 0.05F);
    cam::reset_choices();

    // Developing: the starting look leaves a picture as it is.
    auto image = grey(64, 48, 128U);
    cam::develop(&image, 7U);
    for (std::size_t i = 0; i < image.pixels.size(); ++i) assert(image.pixels[i] == (i % 4U == 3U ? 255U : 128U));
    // +1 EV brightens, a monochrome style has no colour, tungsten balance cools.
    choose(cam::Key::ExposureCompensation, 7);
    image = grey(8, 8, 100U);
    cam::develop(&image, 1U);
    assert(image.pixels[0] > 120U);
    cam::reset_choices();
    choose(cam::Key::ColourBalance, 4);
    image = grey(8, 8, 128U);
    cam::develop(&image, 1U);
    assert(image.pixels[2] > image.pixels[0]);
    cam::reset_choices();
    choose(cam::Key::ColourStyle, 3);
    image = grey(8, 8, 0U);
    for (std::size_t i = 0; i < image.pixels.size(); i += 4) image.pixels[i] = 200U;
    cam::develop(&image, 1U);
    assert(image.pixels[0] == image.pixels[1] && image.pixels[1] == image.pixels[2]);
    cam::reset_choices();
    // ISO 6400 noise moves pixels, and differently from frame to frame.
    choose(cam::Key::ExposureIso, 8);
    auto a = grey(32, 32, 128U);
    auto b = grey(32, 32, 128U);
    cam::develop(&a, 1U);
    cam::develop(&b, 2U);
    int moved = 0;
    int differ = 0;
    for (std::size_t i = 0; i < a.pixels.size(); i += 4) {
        moved += a.pixels[i] != 128U ? 1 : 0;
        differ += a.pixels[i] != b.pixels[i] ? 1 : 0;
    }
    assert(moved > 500 && differ > 500);
    cam::reset_choices();
    // Vignetting darkens the corners, not the centre.
    choose(cam::Key::LensVignette, 2);
    image = grey(65, 65, 200U);
    cam::develop(&image, 1U);
    assert(image.pixels[0] < 120U && image.pixels[(32U * 65U + 32U) * 4U] == 200U);
    cam::reset_choices();

    // Megapixels in the picture's shape: 12 MP at 4:3 is 4000 x 3000; 124 MP
    // at 3:2 is past one render (drawn in tiles); 2.35:1 is a scope frame.
    choose(cam::Key::PhotoMegapixels, 4);
    choose(cam::Key::PhotoAspect, 3);
    photo = cam::plan(cam::Kind::Photo, 2400, 1080);
    assert(photo.width == 4000 && photo.height == 3000);
    choose(cam::Key::PhotoMegapixels, 13);
    choose(cam::Key::PhotoAspect, 2);
    photo = cam::plan(cam::Kind::Photo, 2400, 1080);
    assert(photo.width == 13638 && photo.height == 9092);
    choose(cam::Key::PhotoMegapixels, 15);  // 200 MP at 2.35:1
    choose(cam::Key::PhotoAspect, 8);
    photo = cam::plan(cam::Kind::Photo, 2400, 1080);
    assert(photo.width == 21678 && photo.height == 9224);
    cam::reset_choices();
    choose(cam::Key::VideoAspect, 6);       // 2.35:1 video
    video = cam::plan(cam::Kind::Video, 2400, 1080);
    assert(video.height == 1072 && video.width == 2528);
    cam::reset_choices();
    // A 400 mm lens.
    choose(cam::Key::LensFocal, 12);
    assert(std::abs(cam::focal_mm(cam::capture_zoom(cam::Kind::Photo, 1.0F, 2400, 1080)) - 400.0F) < 0.1F);
    cam::reset_choices();

    // Depth of field: a far wall behind a near, sharp square. Focused on the
    // square (auto, the middle), the square stays sharp and the wall's
    // stripes blur; at f/22 they stay sharper than at f/1.4.
    {
        const auto scene = [](std::vector<float>* depth) {
            auto picture = grey(200, 120, 0U);
            depth->assign(200U * 120U, 2000.0F);
            for (int y = 0; y < 120; ++y) {
                for (int x = 0; x < 200; ++x) {
                    auto* px = &picture.pixels[(static_cast<std::size_t>(y) * 200U + x) * 4U];
                    const bool square = x >= 80 && x < 120 && y >= 40 && y < 80;
                    const std::uint8_t v = square ? ((x / 2) % 2 == 0 ? 255U : 0U) : ((x / 4) % 2 == 0 ? 255U : 0U);
                    px[0] = px[1] = px[2] = v;
                    if (square) (*depth)[static_cast<std::size_t>(y) * 200U + x] = 150.0F;
                }
            }
            return picture;
        };
        const auto contrast_at = [](const RgbaImage& picture, int x0, int y) {
            int lo = 255;
            int hi = 0;
            for (int x = x0; x < x0 + 8; ++x) {
                const int v = picture.pixels[(static_cast<std::size_t>(y) * 200U + x) * 4U];
                lo = std::min(lo, v);
                hi = std::max(hi, v);
            }
            return hi - lo;
        };
        choose(cam::Key::LensDepthOfField, 1);
        choose(cam::Key::LensAperture, 0);  // f/1.4
        std::vector<float> depth;
        auto wide = scene(&depth);
        cam::develop(&wide, 1U, 85.0F, &depth);
        assert(std::abs(cam::last_focus() - 150.0F) < 1e-3F);
        assert(contrast_at(wide, 96, 60) > 200);  // the square: sharp
        assert(contrast_at(wide, 20, 10) < 60);   // the wall: blurred
        choose(cam::Key::LensAperture, 8);  // f/22
        auto narrow = scene(&depth);
        cam::develop(&narrow, 1U, 85.0F, &depth);
        assert(contrast_at(narrow, 20, 10) > contrast_at(wide, 20, 10));
        // Off, nothing moves.
        cam::reset_choices();
        auto off = scene(&depth);
        cam::develop(&off, 1U, 85.0F, &depth);
        assert(contrast_at(off, 20, 10) == 255);
    }

    // DNG: a TIFF whose first IFD is the developed preview and whose SubIFD
    // is the RAW: 16-bit linear RGB (or one Bayer sample a pixel) filling the
    // file to its end; full scale where the light is full.
    for (const bool bayer : {false, true}) {
        const std::string path = std::string(bayer ? "photo_camera_bayer" : "photo_camera_linear") + ".dng";
        const int fd = ::open(path.c_str(), O_CREAT | O_TRUNC | O_WRONLY, 0644);
        assert(fd >= 0);
        auto optical = grey(10, 6, 255U);
        auto preview = grey(5, 3, 128U);
        assert(cam::write_dng(fd, optical, preview, bayer, 1U, 50.0F));
        ::close(fd);
        std::ifstream in(path, std::ios::binary);
        const std::vector<std::uint8_t> file((std::istreambuf_iterator<char>(in)), {});
        std::remove(path.c_str());
        const auto u16 = [&](std::size_t at) { return static_cast<std::uint32_t>(file[at] | file[at + 1U] << 8U); };
        const auto u32 = [&](std::size_t at) { return u16(at) | u16(at + 2U) << 16U; };
        assert(file.size() > 8U && file[0] == 'I' && file[1] == 'I' && u16(2) == 42U);
        const auto tag = [&](std::uint32_t ifd, std::uint32_t wanted) -> std::uint32_t {
            for (std::uint32_t i = 0; i < u16(ifd); ++i) {
                const std::size_t e = ifd + 2U + i * 12U;
                if (u16(e) == wanted) return u16(e + 2U) == 3U ? u16(e + 8U) : u32(e + 8U);
            }
            return 0xFFFFFFFFU;
        };
        const std::uint32_t main = u32(4);
        assert(tag(main, 256) == 5U && tag(main, 50706) != 0xFFFFFFFFU);
        const std::uint32_t raw = tag(main, 330);
        assert(tag(raw, 256) == 10U && tag(raw, 257) == 6U);
        assert(tag(raw, 262) == (bayer ? 32803U : 34892U));
        const std::uint32_t at = tag(raw, 273);
        const std::uint32_t count = tag(raw, 279);
        assert(count == 10U * 6U * (bayer ? 1U : 3U) * 2U && at + count == file.size());
        assert(u16(at) == 65535U);
    }

    // The viewfinder: off at the start; on, it frames the picture with the
    // thirds and writes the settings; recording frames the video.
    assert(!cam::viewfinder(nullptr, 2400, 1080, 1.0F, false, 0).shown);
    choose(cam::Key::FinderMode, 1);
    auto finder = cam::viewfinder(nullptr, 2400, 1080, 1.0F, false, 0);
    assert(finder.shown && finder.lines.size() == 4U && finder.status.find("ISO 100") != std::string::npos);
    assert(finder.status.find("PNG 2400") != std::string::npos);
    finder = cam::viewfinder(nullptr, 2400, 1080, 1.0F, true, 75);
    assert(finder.status.find("REC 1:15") != std::string::npos && std::abs(finder.frame.x - 240.0F) < 0.5F);
    cam::reset_choices();
    return 0;
}
