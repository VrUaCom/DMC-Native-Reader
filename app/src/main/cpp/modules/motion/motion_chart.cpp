#include "dmcresource/motion/motion_chart.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

#include "dmc_rengine/analysis/mot/channel_binding.hpp"
#include "dmc_rengine/analysis/mot/track_evaluation.hpp"
#include "dmc_rengine/formats/mot/ir.hpp"
#include "dmcresource/motion/motion_clip.h"
#include "dmcresource/raster_card.h"

namespace dmcresource::motion {
namespace {

namespace mot = dmc::rengine::formats::mot;
namespace mot_analysis = dmc::rengine::analysis::mot;

using raster::Canvas;
using raster::Rgb;

[[nodiscard]] Rgb node_colour(std::size_t node, std::size_t nodes, int axis) {
    // Hue per node; x full, y lighter, z lightest.
    const float h = static_cast<float>(node) / static_cast<float>(std::max<std::size_t>(nodes, 1U));
    const float k[3] = {5.0F, 3.0F, 1.0F};
    float rgb[3];
    for (int i = 0; i < 3; ++i) {
        const float t = std::fmod(k[i] + h * 6.0F, 6.0F);
        rgb[i] = 1.0F - std::max(0.0F, std::min({t, 4.0F - t, 1.0F}));
    }
    const float lift = axis * 0.22F;
    Rgb out;
    out.r = static_cast<std::uint8_t>(std::clamp((rgb[0] * (1.0F - lift) + lift) * 235.0F, 0.0F, 255.0F));
    out.g = static_cast<std::uint8_t>(std::clamp((rgb[1] * (1.0F - lift) + lift) * 235.0F, 0.0F, 255.0F));
    out.b = static_cast<std::uint8_t>(std::clamp((rgb[2] * (1.0F - lift) + lift) * 235.0F, 0.0F, 255.0F));
    return out;
}

struct Curve final {
    std::size_t node{};
    int group{};  // 0 rotation, 1 translation, 2 scale
    int axis{};
    std::vector<float> values;
};

}  // namespace

std::optional<ImagePreview> render_motion_chart(const mot::Document& document, int width, int height) {
    try {
        if (width < 200 || height < 200) return std::nullopt;
        const auto binding =
            mot_analysis::project_normal_binding(document, document.channel_domain_count);
        if (!binding) return std::nullopt;
        float end = document.raw_f32_0c;
        if (!(end > 0.0F) || !std::isfinite(end)) end = 60.0F;
        const int samples = std::clamp(static_cast<int>(width) - 120, 64, 2048);

        std::vector<Curve> curves;
        curves.reserve(binding->tracks.size());
        for (const auto& bound : binding->tracks) {
            const auto& track = document.tracks[bound.track_index];
            Curve curve;
            curve.node = bound.node_index;
            const auto ch = static_cast<int>(bound.channel);
            curve.group = ch < 3 ? 1 : (ch < 6 ? 0 : 2);
            curve.axis = ch % 3;
            curve.values.resize(static_cast<std::size_t>(samples));
            std::int32_t cache = 0;
            for (int i = 0; i < samples; ++i) {
                const float frame = end * static_cast<float>(i) / static_cast<float>(samples - 1);
                float value = 0.0F;
                if (track.compression == 3U) {
                    const auto e = mot_analysis::evaluate_compression3_track(track, frame, cache);
                    if (e && std::isfinite(e->value)) {
                        value = e->value;
                        cache = e->cached_index;
                    }
                } else if (track.compression == 2U) {
                    float e = 0.0F;
                    if (evaluate_compression2_track(track, frame, &cache, &e)) value = e;
                }
                curve.values[static_cast<std::size_t>(i)] = value;
            }
            curves.push_back(std::move(curve));
        }

        Canvas canvas{width, height};
        const Rgb background{18, 18, 22}, panel{28, 29, 36}, grid{52, 54, 66}, label{200, 202, 214};
        canvas.fill(0, 0, width, height, background);
        const int scale = std::max(1, width / 360);
        const int header = 12 * scale + 12;
        std::string title = "MOT  NODES " + std::to_string(document.channel_domain_count) +
                            "  TRACKS " + std::to_string(document.tracks.size()) + "  FRAMES " +
                            std::to_string(static_cast<int>(end));
        canvas.text(10, 8, title, label, scale);

        const char* names[3] = {"ROTATION (RAD)", "TRANSLATION", "SCALE"};
        const int left = 10;
        const int right = width - 10;
        const int panel_gap = 8;
        // Panels without curves shrink to their caption; the rest share the space.
        std::array<std::size_t, 3> counts{};
        for (const auto& c : curves) ++counts[static_cast<std::size_t>(c.group)];
        const int empty_h = 9 * scale + 12;
        int filled = 0;
        for (const auto n : counts) filled += n != 0U ? 1 : 0;
        const int empty_total = (3 - filled) * empty_h;
        const int panel_h = filled == 0
            ? empty_h
            : (height - header - 3 * panel_gap - 10 * scale - empty_total) / filled;
        int top = header;
        for (int group = 0; group < 3; ++group) {
            const int this_h = counts[static_cast<std::size_t>(group)] != 0U ? panel_h : empty_h;
            const int bottom = top + this_h;
            const int panel_top = top;
            top = bottom + panel_gap;
            canvas.fill(left, panel_top, right, bottom, panel);
            float lo = 1.0e9F, hi = -1.0e9F;
            std::size_t count = 0U;
            for (const auto& c : curves) {
                if (c.group != group) continue;
                ++count;
                for (const float v : c.values) {
                    lo = std::min(lo, v);
                    hi = std::max(hi, v);
                }
            }
            std::string caption = std::string{names[group]} + "  " + std::to_string(count) + " CURVES";
            canvas.text(left + 6, panel_top + 6, caption, label, scale);
            if (count == 0U) continue;
            if (hi - lo < 1.0e-4F) {
                hi += 0.5F;
                lo -= 0.5F;
            }
            const float pad = (hi - lo) * 0.08F;
            lo -= pad;
            hi += pad;
            const int plot_top = panel_top + 9 * scale + 10;
            const auto to_y = [&](float v) {
                return bottom - 4 - static_cast<int>((v - lo) / (hi - lo) * (bottom - 4 - plot_top));
            };
            // Frame grid every 10 frames, zero line.
            for (int f = 0; f <= static_cast<int>(end); f += 10) {
                const int x = left + static_cast<int>((right - left) * (static_cast<float>(f) / end));
                canvas.line(x, plot_top, x, bottom - 2, grid);
            }
            if (lo < 0.0F && hi > 0.0F) canvas.line(left, to_y(0.0F), right, to_y(0.0F), grid);
            char range[64];
            std::snprintf(range, sizeof(range), "%.2f / %.2f", static_cast<double>(lo + pad),
                          static_cast<double>(hi - pad));
            canvas.text(right - Canvas::text_width(range, scale) - 6, panel_top + 6, range, label, scale);
            for (const auto& c : curves) {
                if (c.group != group) continue;
                const auto colour = node_colour(c.node, document.channel_domain_count, c.axis);
                int px = left, py = to_y(c.values.front());
                for (int i = 1; i < samples; ++i) {
                    const int x = left + (right - left) * i / (samples - 1);
                    const int y = to_y(c.values[static_cast<std::size_t>(i)]);
                    canvas.line(px, py, x, y, colour);
                    px = x;
                    py = y;
                }
            }
        }
        // Frame axis labels under the last panel.
        const int axis_y = height - 9 * scale - 4;
        const int step = end > 120.0F ? 30 : 10;
        for (int f = 0; f <= static_cast<int>(end); f += step) {
            const int x = left + static_cast<int>((right - left) * (static_cast<float>(f) / end));
            canvas.text(std::min(x, right - 18 * scale), axis_y, std::to_string(f), label, scale);
        }
        return canvas.take();
    } catch (...) {
        return std::nullopt;
    }
}

}  // namespace dmcresource::motion
