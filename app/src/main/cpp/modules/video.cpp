#include "dmcresource/video.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <utility>

#if defined(DMC_NATIVE_READER_FFMPEG)
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/channel_layout.h>
#include <libavutil/mem.h>
#include <libavutil/pixfmt.h>
#include <libavutil/samplefmt.h>
}
#endif

namespace dmcresource::video {

bool looks_like_asf(std::span<const std::uint8_t> bytes) noexcept {
    static constexpr std::uint8_t kHeader[16] = {0x30, 0x26, 0xB2, 0x75, 0x8E, 0x66, 0xCF, 0x11,
                                                 0xA6, 0xD9, 0x00, 0xAA, 0x00, 0x62, 0xCE, 0x6C};
    return bytes.size() >= 30U && std::memcmp(bytes.data(), kHeader, sizeof kHeader) == 0;
}

RgbaImage fit(const RgbaImage& picture, int width, int height) {
    RgbaImage out;
    if (width <= 0 || height <= 0) return out;
    out.width = width;
    out.height = height;
    out.pixels.assign(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4U, 0U);
    for (std::size_t i = 3U; i < out.pixels.size(); i += 4U) out.pixels[i] = 255U;
    if (picture.width <= 0 || picture.height <= 0 ||
        picture.pixels.size() < static_cast<std::size_t>(picture.width) * static_cast<std::size_t>(picture.height) * 4U) {
        return out;
    }
    const double scale = std::min(static_cast<double>(width) / picture.width, static_cast<double>(height) / picture.height);
    const int w = std::max(1, static_cast<int>(std::lround(picture.width * scale)));
    const int h = std::max(1, static_cast<int>(std::lround(picture.height * scale)));
    const int x0 = (width - w) / 2, y0 = (height - h) / 2;
    const auto at = [&picture](int x, int y) {
        x = std::clamp(x, 0, picture.width - 1);
        y = std::clamp(y, 0, picture.height - 1);
        return picture.pixels.data() + (static_cast<std::size_t>(y) * static_cast<std::size_t>(picture.width) +
                                        static_cast<std::size_t>(x)) * 4U;
    };
    for (int y = 0; y < h; ++y) {
        const double sy = (y + 0.5) / scale - 0.5;
        const int iy = static_cast<int>(std::floor(sy));
        const double fy = sy - iy;
        auto* row = out.pixels.data() + (static_cast<std::size_t>(y0 + y) * static_cast<std::size_t>(width) +
                                         static_cast<std::size_t>(x0)) * 4U;
        for (int x = 0; x < w; ++x) {
            const double sx = (x + 0.5) / scale - 0.5;
            const int ix = static_cast<int>(std::floor(sx));
            const double fx = sx - ix;
            const auto* a = at(ix, iy);
            const auto* b = at(ix + 1, iy);
            const auto* c = at(ix, iy + 1);
            const auto* d = at(ix + 1, iy + 1);
            for (int k = 0; k < 3; ++k) {
                const double top = a[k] + (b[k] - a[k]) * fx;
                const double bottom = c[k] + (d[k] - c[k]) * fx;
                row[x * 4 + k] = static_cast<std::uint8_t>(std::clamp(top + (bottom - top) * fy + 0.5, 0.0, 255.0));
            }
        }
    }
    return out;
}

#if defined(DMC_NATIVE_READER_FFMPEG)

bool available() noexcept { return true; }

namespace {

// The file bytes as an FFmpeg input (custom AVIO over memory).
struct Reader final {
    std::shared_ptr<const std::vector<std::uint8_t>> bytes;
    std::int64_t position{};

    static int read(void* opaque, std::uint8_t* buffer, int size) {
        auto& r = *static_cast<Reader*>(opaque);
        const auto total = static_cast<std::int64_t>(r.bytes->size());
        if (r.position >= total) return AVERROR_EOF;
        const auto n = static_cast<int>(std::min<std::int64_t>(size, total - r.position));
        std::memcpy(buffer, r.bytes->data() + r.position, static_cast<std::size_t>(n));
        r.position += n;
        return n;
    }

    static std::int64_t seek(void* opaque, std::int64_t offset, int whence) {
        auto& r = *static_cast<Reader*>(opaque);
        const auto total = static_cast<std::int64_t>(r.bytes->size());
        switch (whence & ~AVSEEK_FORCE) {
        case AVSEEK_SIZE: return total;
        case SEEK_SET: r.position = offset; break;
        case SEEK_CUR: r.position += offset; break;
        case SEEK_END: r.position = total + offset; break;
        default: return -1;
        }
        r.position = std::clamp<std::int64_t>(r.position, 0, total);
        return r.position;
    }
};

// One demuxer and the decoder of one of its streams.
class Stream final {
public:
    Stream(const Stream&) = delete;
    Stream& operator=(const Stream&) = delete;
    ~Stream() {
        av_frame_free(&frame_);
        av_packet_free(&packet_);
        avcodec_free_context(&codec_);
        if (format_ != nullptr) avformat_close_input(&format_);
        if (io_ != nullptr) {
            av_freep(&io_->buffer);
            avio_context_free(&io_);
        }
    }

    static std::unique_ptr<Stream> open(std::shared_ptr<const std::vector<std::uint8_t>> bytes, AVMediaType type,
                                        std::string* error) {
        std::unique_ptr<Stream> s{new Stream};
        s->reader_.bytes = std::move(bytes);
        constexpr int kBuffer = 1 << 16;
        auto* buffer = static_cast<unsigned char*>(av_malloc(kBuffer));
        if (buffer == nullptr) return nullptr;
        s->io_ = avio_alloc_context(buffer, kBuffer, 0, &s->reader_, &Reader::read, nullptr, &Reader::seek);
        if (s->io_ == nullptr) {
            av_free(buffer);
            return nullptr;
        }
        s->format_ = avformat_alloc_context();
        if (s->format_ == nullptr) return nullptr;
        s->format_->pb = s->io_;
        if (avformat_open_input(&s->format_, nullptr, nullptr, nullptr) < 0) {
            s->format_ = nullptr;  // freed by the failed open
            if (error != nullptr) *error = "not a movie FFmpeg can read";
            return nullptr;
        }
        if (avformat_find_stream_info(s->format_, nullptr) < 0) {
            if (error != nullptr) *error = "its streams could not be read";
            return nullptr;
        }
        const AVCodec* decoder = nullptr;
        s->index_ = av_find_best_stream(s->format_, type, -1, -1, &decoder, 0);
        if (s->index_ < 0 || decoder == nullptr) {
            if (error != nullptr) {
                *error = type == AVMEDIA_TYPE_VIDEO ? "no video stream this build decodes" : "no sound stream";
            }
            return nullptr;
        }
        s->codec_ = avcodec_alloc_context3(decoder);
        if (s->codec_ == nullptr ||
            avcodec_parameters_to_context(s->codec_, s->format_->streams[s->index_]->codecpar) < 0 ||
            avcodec_open2(s->codec_, decoder, nullptr) < 0) {
            if (error != nullptr) *error = std::string{"its "} + decoder->name + " stream could not be opened";
            return nullptr;
        }
        s->packet_ = av_packet_alloc();
        s->frame_ = av_frame_alloc();
        if (s->packet_ == nullptr || s->frame_ == nullptr) return nullptr;
        return s;
    }

    [[nodiscard]] AVStream* stream() const noexcept { return format_->streams[index_]; }
    [[nodiscard]] AVFormatContext* format() const noexcept { return format_; }
    [[nodiscard]] AVCodecContext* codec() const noexcept { return codec_; }

    // Seconds from the stream's start of a decoded frame.
    [[nodiscard]] double seconds(const AVFrame* f) const noexcept {
        const auto* st = stream();
        std::int64_t ts = f->best_effort_timestamp != AV_NOPTS_VALUE ? f->best_effort_timestamp : f->pts;
        if (ts == AV_NOPTS_VALUE) ts = 0;
        const std::int64_t start = st->start_time != AV_NOPTS_VALUE ? st->start_time : 0;
        return static_cast<double>(ts - start) * av_q2d(st->time_base);
    }

    // The next decoded frame of the stream (owned by the stream until the
    // next call); null at the end.
    AVFrame* next() {
        for (;;) {
            const int got = avcodec_receive_frame(codec_, frame_);
            if (got == 0) return frame_;
            if (got == AVERROR_EOF) return nullptr;
            if (got != AVERROR(EAGAIN)) return nullptr;
            if (draining_) return nullptr;
            const int read = av_read_frame(format_, packet_);
            if (read < 0) {
                draining_ = true;
                (void)avcodec_send_packet(codec_, nullptr);
                continue;
            }
            if (packet_->stream_index == index_) (void)avcodec_send_packet(codec_, packet_);
            av_packet_unref(packet_);
        }
    }

    // Back to the key frame at or before `seconds`.
    void seek(double seconds) {
        const auto* st = stream();
        const std::int64_t start = st->start_time != AV_NOPTS_VALUE ? st->start_time : 0;
        const auto ts = start + static_cast<std::int64_t>(std::max(0.0, seconds) / av_q2d(st->time_base));
        if (av_seek_frame(format_, index_, ts, AVSEEK_FLAG_BACKWARD) < 0) {
            (void)av_seek_frame(format_, index_, start, AVSEEK_FLAG_BACKWARD | AVSEEK_FLAG_ANY);
        }
        avcodec_flush_buffers(codec_);
        draining_ = false;
    }

private:
    Stream() = default;
    Reader reader_;
    AVIOContext* io_{};
    AVFormatContext* format_{};
    AVCodecContext* codec_{};
    AVPacket* packet_{};
    AVFrame* frame_{};
    int index_{-1};
    bool draining_{};
};

// BT.601 (limited range, as Windows Media video is coded; full range when
// the frame says so), integer.
[[nodiscard]] bool to_rgba(const AVFrame* f, RgbaImage* out) {
    if (f->format != AV_PIX_FMT_YUV420P && f->format != AV_PIX_FMT_YUVJ420P) return false;
    const bool full = f->format == AV_PIX_FMT_YUVJ420P || f->color_range == AVCOL_RANGE_JPEG;
    out->width = f->width;
    out->height = f->height;
    out->pixels.resize(static_cast<std::size_t>(f->width) * static_cast<std::size_t>(f->height) * 4U);
    for (int y = 0; y < f->height; ++y) {
        const std::uint8_t* py = f->data[0] + static_cast<std::ptrdiff_t>(y) * f->linesize[0];
        const std::uint8_t* pu = f->data[1] + static_cast<std::ptrdiff_t>(y / 2) * f->linesize[1];
        const std::uint8_t* pv = f->data[2] + static_cast<std::ptrdiff_t>(y / 2) * f->linesize[2];
        auto* row = out->pixels.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(f->width) * 4U;
        for (int x = 0; x < f->width; ++x) {
            const int d = pu[x / 2] - 128, e = pv[x / 2] - 128;
            int r = 0, g = 0, b = 0;
            if (full) {
                const int c = 256 * py[x];
                r = (c + 359 * e + 128) >> 8;
                g = (c - 88 * d - 183 * e + 128) >> 8;
                b = (c + 454 * d + 128) >> 8;
            } else {
                const int c = 298 * (py[x] - 16);
                r = (c + 409 * e + 128) >> 8;
                g = (c - 100 * d - 208 * e + 128) >> 8;
                b = (c + 516 * d + 128) >> 8;
            }
            row[x * 4 + 0] = static_cast<std::uint8_t>(std::clamp(r, 0, 255));
            row[x * 4 + 1] = static_cast<std::uint8_t>(std::clamp(g, 0, 255));
            row[x * 4 + 2] = static_cast<std::uint8_t>(std::clamp(b, 0, 255));
            row[x * 4 + 3] = 255U;
        }
    }
    return true;
}

[[nodiscard]] float sample_of(const AVFrame* f, int channel, int index) {
    const auto format = static_cast<AVSampleFormat>(f->format);
    const int channels = f->ch_layout.nb_channels;
    const bool planar = av_sample_fmt_is_planar(format) != 0;
    const std::uint8_t* base = planar ? f->extended_data[channel] : f->extended_data[0];
    const int at = planar ? index : index * channels + channel;
    switch (av_get_packed_sample_fmt(format)) {
    case AV_SAMPLE_FMT_U8: return (base[at] - 128) / 128.0F;
    case AV_SAMPLE_FMT_S16: return reinterpret_cast<const std::int16_t*>(base)[at] / 32768.0F;
    case AV_SAMPLE_FMT_S32: return static_cast<float>(reinterpret_cast<const std::int32_t*>(base)[at] / 2147483648.0);
    case AV_SAMPLE_FMT_FLT: return reinterpret_cast<const float*>(base)[at];
    case AV_SAMPLE_FMT_DBL: return static_cast<float>(reinterpret_cast<const double*>(base)[at]);
    default: return 0.0F;
    }
}

class FfmpegMovie final : public Movie {
public:
    FfmpegMovie(std::shared_ptr<const std::vector<std::uint8_t>> bytes, std::unique_ptr<Stream> video)
        : bytes_(std::move(bytes)), video_(std::move(video)) {
        auto* codec = video_->codec();
        info_.width = codec->width;
        info_.height = codec->height;
        info_.video_codec = codec->codec != nullptr ? codec->codec->name : "";
        const auto* st = video_->stream();
        const auto rate = st->avg_frame_rate.num > 0 ? st->avg_frame_rate : st->r_frame_rate;
        info_.frame_rate = rate.num > 0 && rate.den > 0 ? av_q2d(rate) : 0.0;
        const auto* format = video_->format();
        if (format->duration != AV_NOPTS_VALUE) {
            info_.duration = static_cast<double>(format->duration) / AV_TIME_BASE;
        } else if (st->duration != AV_NOPTS_VALUE) {
            info_.duration = static_cast<double>(st->duration) * av_q2d(st->time_base);
        }
        // The sound stream's identity, without decoding it yet.
        for (unsigned i = 0; i < format->nb_streams; ++i) {
            const auto* par = format->streams[i]->codecpar;
            if (par->codec_type != AVMEDIA_TYPE_AUDIO) continue;
            const auto* decoder = avcodec_find_decoder(par->codec_id);
            info_.audio_codec = decoder != nullptr ? decoder->name : avcodec_get_name(par->codec_id);
            info_.audio_rate = static_cast<std::uint32_t>(std::max(par->sample_rate, 0));
            info_.audio_channels = static_cast<std::uint32_t>(std::max(par->ch_layout.nb_channels, 0));
            break;
        }
    }

    const MovieInfo& info() const noexcept override { return info_; }

    ~FfmpegMovie() override {
        av_frame_free(&shown_);
        av_frame_free(&pending_);
    }

    bool frame_at(double seconds, RgbaImage* out) override {
        if (out == nullptr || !std::isfinite(seconds)) return false;
        try {
            seconds = std::max(0.0, seconds);
            const bool shown = shown_ != nullptr;
            // Inside the shown frame's span (or past the last frame): nothing
            // to decode.
            if (shown && seconds >= shown_time_ && (ended_ || (pending_ != nullptr && seconds < pending_time_))) {
                return picture(out);
            }
            // Back, or far ahead: from the key frame before it.
            if (!shown || seconds < shown_time_ || seconds > shown_time_ + 3.0) {
                video_->seek(seconds);
                av_frame_free(&shown_);
                av_frame_free(&pending_);
                ended_ = false;
            }
            for (;;) {
                if (pending_ != nullptr) {
                    if (pending_time_ > seconds && shown_ != nullptr) break;
                    av_frame_free(&shown_);
                    shown_ = pending_;
                    shown_time_ = pending_time_;
                    pending_ = nullptr;
                    converted_ = false;
                }
                const AVFrame* f = video_->next();
                if (f == nullptr) {
                    ended_ = true;
                    break;
                }
                pending_ = av_frame_clone(f);
                if (pending_ == nullptr) return false;
                pending_time_ = video_->seconds(f);
            }
            return shown_ != nullptr && picture(out);
        } catch (...) {
            return false;
        }
    }

    std::optional<sound::Pcm> sound() override {
        try {
            auto audio = Stream::open(bytes_, AVMEDIA_TYPE_AUDIO, nullptr);
            if (!audio) return std::nullopt;
            sound::Pcm pcm;
            pcm.sample_rate = static_cast<std::uint32_t>(std::max(audio->codec()->sample_rate, 1));
            pcm.channels = static_cast<std::uint32_t>(std::clamp(audio->codec()->ch_layout.nb_channels, 1, 2));
            while (const AVFrame* f = audio->next()) {
                const int channels = f->ch_layout.nb_channels;
                if (channels <= 0) continue;
                for (int i = 0; i < f->nb_samples; ++i) {
                    for (std::uint32_t c = 0; c < pcm.channels; ++c) {
                        const float v = sample_of(f, std::min<int>(static_cast<int>(c), channels - 1), i);
                        pcm.samples.push_back(static_cast<std::int16_t>(std::clamp(v * 32767.0F, -32768.0F, 32767.0F)));
                    }
                }
            }
            if (pcm.samples.empty()) return std::nullopt;
            return pcm;
        } catch (...) {
            return std::nullopt;
        }
    }

private:
    std::shared_ptr<const std::vector<std::uint8_t>> bytes_;
    std::unique_ptr<Stream> video_;
    // The shown frame as RGBA, converted once.
    bool picture(RgbaImage* out) {
        if (!converted_) {
            if (!to_rgba(shown_, &rgba_)) return false;
            converted_ = true;
        }
        *out = rgba_;
        return true;
    }

    MovieInfo info_;
    // The frame shown now and the one decoded after it.
    AVFrame* shown_{};
    double shown_time_{};
    AVFrame* pending_{};
    double pending_time_{};
    bool ended_{};
    RgbaImage rgba_;
    bool converted_{};
};

}  // namespace

std::unique_ptr<Movie> open_movie(std::shared_ptr<const std::vector<std::uint8_t>> bytes, std::string* error) {
    try {
        if (!bytes || bytes->empty()) return nullptr;
        av_log_set_level(AV_LOG_QUIET);
        auto video = Stream::open(bytes, AVMEDIA_TYPE_VIDEO, error);
        if (!video) return nullptr;
        return std::make_unique<FfmpegMovie>(std::move(bytes), std::move(video));
    } catch (...) {
        if (error != nullptr) *error = "the movie could not be read";
        return nullptr;
    }
}

#else

bool available() noexcept { return false; }

std::unique_ptr<Movie> open_movie(std::shared_ptr<const std::vector<std::uint8_t>>, std::string* error) {
    if (error != nullptr) *error = "this build has no movie decoder";
    return nullptr;
}

#endif

}  // namespace dmcresource::video
