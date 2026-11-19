// Movies (video.h) and a movie as a timeline of its own, or a cutscene's
// movie part (demo_session.h): synthetic bytes only.
#include <cassert>
#include <cmath>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "dmcresource/demo_session.h"
#include "dmcresource/motion/motion_player.h"
#include "dmcresource/resource_session.h"
#include "dmcresource/video.h"
#include "video_tiny_wmv.h"

using namespace dmcresource;

namespace {

[[nodiscard]] std::uint64_t checksum(const RgbaImage& image) {
    std::uint64_t sum = 0U;
    for (std::size_t i = 0; i < image.pixels.size(); ++i) sum = sum * 31U + image.pixels[i];
    return sum;
}

}  // namespace

namespace {

// An HD "ipum" stream of DXT1 frames 4 x 4: each frame one block of one colour.
std::vector<std::uint8_t> ipum(const std::vector<std::uint16_t>& colours) {
    std::vector<std::uint8_t> out{'i', 'p', 'u', 'm', 0, 0, 1, 0, 4, 0, 4, 0};
    out.push_back(static_cast<std::uint8_t>(colours.size()));
    out.insert(out.end(), {0, 0, 0});
    for (const auto colour : colours) {
        out.insert(out.end(), {'f', 'r', 'm', 'j', 0x80, 0x80, 0, 0});
        std::vector<std::uint8_t> dds(128U, 0U);
        const auto put = [&dds](std::size_t at, std::uint32_t v) {
            for (int i = 0; i < 4; ++i) dds[at + i] = static_cast<std::uint8_t>(v >> (8 * i));
        };
        dds[0] = 'D'; dds[1] = 'D'; dds[2] = 'S'; dds[3] = ' ';
        put(4, 124U); put(8, 0x81007U); put(12, 4U); put(16, 4U); put(20, 8U);
        put(76, 32U); put(80, 4U); put(84, 0x31545844U); put(108, 0x1000U);
        out.insert(out.end(), dds.begin(), dds.end());
        const std::uint8_t block[8] = {static_cast<std::uint8_t>(colour), static_cast<std::uint8_t>(colour >> 8),
                                       static_cast<std::uint8_t>(colour), static_cast<std::uint8_t>(colour >> 8),
                                       0, 0, 0, 0};
        out.insert(out.end(), std::begin(block), std::end(block));
    }
    return out;
}

}  // namespace

int main() {
    // An ipum stream: its DDS frames at 25 a second, read without FFmpeg.
    {
        const auto bytes = ipum({0xF800, 0x001F, 0x07E0});
        assert(video::looks_like_ipu(bytes) && !video::looks_like_asf(bytes));
        std::string error;
        auto frames = video::open_movie(std::make_shared<const std::vector<std::uint8_t>>(bytes), &error);
        assert(frames && frames->info().width == 4 && frames->info().height == 4);
        assert(std::fabs(frames->info().duration - 3.0 / 25.0) < 1e-9 && frames->info().frame_rate == 25.0);
        RgbaImage picture;
        assert(frames->frame_at(0.0, &picture) && picture.width == 4 && picture.pixels[0] > 240 && picture.pixels[2] < 16);
        assert(frames->frame_at(1.0 / 25.0 + 0.001, &picture) && picture.pixels[2] > 240 && picture.pixels[0] < 16);
        assert(frames->frame_at(10.0, &picture) && picture.pixels[1] > 240);  // held on the last frame
        assert(!frames->sound());
        auto session = open_session("x.ipu", bytes.data(), bytes.size());
        assert(session && session->probe.format == Format::Ipu);
        // Not a frame stream: a PS2 IPU stream proper is refused with a reason.
        auto ps2 = bytes;
        ps2.resize(16U);
        ps2.insert(ps2.end(), {0, 0, 1, 0xB0, 1, 2, 3, 4});
        assert(!video::open_movie(std::make_shared<const std::vector<std::uint8_t>>(ps2), &error) && !error.empty());
    }
    const std::span<const std::uint8_t> file{kTinyWmv, sizeof kTinyWmv};
    assert(video::looks_like_asf(file));
    assert(!video::looks_like_asf(file.subspan(1)));

    // Letterboxing: a wide picture in a square keeps its proportions, black
    // above and below.
    {
        RgbaImage wide;
        wide.width = 4;
        wide.height = 2;
        wide.pixels.assign(4U * 2U * 4U, 200U);
        const auto fitted = video::fit(wide, 8, 8);
        assert(fitted.width == 8 && fitted.height == 8);
        assert(fitted.pixels[0] == 0U && fitted.pixels[3] == 255U);           // top row: black, opaque
        assert(fitted.pixels[(4U * 8U + 4U) * 4U] == 200U);                   // centre: the picture
    }

    if (!video::available()) {
        std::string error;
        assert(video::open_movie(std::make_shared<const std::vector<std::uint8_t>>(file.begin(), file.end()), &error) ==
               nullptr);
        assert(!error.empty());
        return 0;
    }

    auto movie = video::open_movie(std::make_shared<const std::vector<std::uint8_t>>(file.begin(), file.end()));
    assert(movie != nullptr);
    const auto& info = movie->info();
    assert(info.width == 64 && info.height == 48);
    assert(info.duration > 0.9 && info.duration < 1.2);
    assert(info.video_codec == "wmv2" && info.audio_codec == "wmav2");
    assert(info.audio_rate == 22050U && info.audio_channels == 1U);

    // Frames by time: forward, then back (a seek), then the same again.
    RgbaImage first, later, again;
    assert(movie->frame_at(0.0, &first) && first.width == 64 && first.height == 48);
    assert(movie->frame_at(0.55, &later) && checksum(later) != checksum(first));
    assert(movie->frame_at(0.0, &again) && checksum(again) == checksum(first));
    // Past the end: the last frame.
    RgbaImage last;
    assert(movie->frame_at(30.0, &last) && last.width == 64);

    const auto pcm = movie->sound();
    assert(pcm && pcm->sample_rate == 22050U && pcm->channels == 1U);
    assert(pcm->frames() > 20000U);
    // The sine is loud.
    int peak = 0;
    for (const auto s : pcm->samples) peak = std::max(peak, std::abs(static_cast<int>(s)));
    assert(peak > 2000);

    // Resampling keeps the length in seconds.
    const auto resampled = sound::resample(*pcm, 44100U);
    assert(resampled.sample_rate == 44100U && resampled.channels == 1U);
    assert(std::abs(static_cast<long>(resampled.frames()) - static_cast<long>(pcm->frames() * 2U)) <= 2);

    // The file opens as a timeline of its own: bound like a cutscene, 60
    // timeline frames a second, the picture is the movie's frame.
    auto session = open_session("tiny.wmv", kTinyWmv, sizeof kTinyWmv);
    assert(session && session->probe.format == Format::Wmv);
    assert(demo::is_demo(session.get()) && session->renderable);
    assert(session->motion_library.size() == 1U && session->motion_library.front().name == "Movie tiny");
    assert(motion::load_library_motion(session.get(), 0).ok);
    const float end = motion::motion_end_frame(session.get());
    assert(end > 55.0F && end < 70.0F);
    assert(motion::apply_motion_frame(session.get(), 33.0F));
    const auto picture = render_session(session.get(), 128, 128, 0.0F, 0.0F, 1.0F, 0U);
    assert(picture.width == 128 && picture.height == 128);
    // 64 x 48 fitted into 128 x 128: 128 x 96, 16 black rows above.
    assert(picture.pixels[0] == 0U && picture.pixels[1] == 0U && picture.pixels[2] == 0U);
    std::size_t lit = 0U;
    for (std::size_t y = 16; y < 112; ++y) {
        for (std::size_t x = 0; x < 128; ++x) lit += picture.pixels[(y * 128U + x) * 4U] > 40U ? 1U : 0U;
    }
    assert(lit > 2000U);
    // Its sound is the timeline's.
    const auto format = demo::audio_format(session.get());
    assert(format && format->sample_rate == 22050U);
    const auto mix = demo::audio_read(session.get(), demo::audio_position(session.get(), 30.0F), 1000U);
    assert(mix.size() == 1000U);
    assert(demo::status_line(session.get()).find("tiny.wmv") != std::string::npos);
    return 0;
}
