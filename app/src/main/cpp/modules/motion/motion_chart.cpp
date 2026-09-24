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

namespace dmcresource::motion {
namespace {

namespace mot = dmc::rengine::formats::mot;
namespace mot_analysis = dmc::rengine::analysis::mot;

struct Rgb final {
    std::uint8_t r{}, g{}, b{};
};

// 5x7 glyphs, one row per byte, bit 4 = left column.
struct Glyph final {
    char c;
    std::array<std::uint8_t, 7> rows;
};

constexpr std::array<Glyph, 44> kFont{{
    {'0', {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E}}, {'1', {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E}},
    {'2', {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F}}, {'3', {0x1F, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0E}},
    {'4', {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02}}, {'5', {0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E}},
    {'6', {0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E}}, {'7', {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08}},
    {'8', {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E}}, {'9', {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C}},
    {'A', {0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}}, {'B', {0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E}},
    {'C', {0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E}}, {'D', {0x1C, 0x12, 0x11, 0x11, 0x11, 0x12, 0x1C}},
    {'E', {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F}}, {'F', {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10}},
    {'G', {0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0F}}, {'H', {0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}},
    {'I', {0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E}}, {'J', {0x07, 0x02, 0x02, 0x02, 0x02, 0x12, 0x0C}},
    {'K', {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11}}, {'L', {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F}},
    {'M', {0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11}}, {'N', {0x11, 0x11, 0x19, 0x15, 0x13, 0x11, 0x11}},
    {'O', {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}}, {'P', {0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10}},
    {'Q', {0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D}}, {'R', {0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11}},
    {'S', {0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E}}, {'T', {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04}},
    {'U', {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}}, {'V', {0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04}},
    {'W', {0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0A}}, {'X', {0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11}},
    {'Y', {0x11, 0x11, 0x11, 0x0A, 0x04, 0x04, 0x04}}, {'Z', {0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F}},
    {'.', {0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C}}, {'-', {0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00}},
    {':', {0x00, 0x0C, 0x0C, 0x00, 0x0C, 0x0C, 0x00}}, {'/', {0x01, 0x01, 0x02, 0x04, 0x08, 0x10, 0x10}},
    {'(', {0x02, 0x04, 0x08, 0x08, 0x08, 0x04, 0x02}}, {')', {0x08, 0x04, 0x02, 0x02, 0x02, 0x04, 0x08}},
    {'+', {0x00, 0x04, 0x04, 0x1F, 0x04, 0x04, 0x00}}, {' ', {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}},
}};

class Canvas final {
public:
    Canvas(int w, int h) : w_(w), h_(h), px_(static_cast<std::size_t>(w) * h * 4U) {}

    void fill(int x0, int y0, int x1, int y1, Rgb c) {
        for (int y = std::max(0, y0); y < std::min(h_, y1); ++y) {
            for (int x = std::max(0, x0); x < std::min(w_, x1); ++x) put(x, y, c);
        }
    }

    void put(int x, int y, Rgb c) {
        if (x < 0 || y < 0 || x >= w_ || y >= h_) return;
        const auto o = (static_cast<std::size_t>(y) * w_ + x) * 4U;
        px_[o] = c.r;
        px_[o + 1U] = c.g;
        px_[o + 2U] = c.b;
        px_[o + 3U] = 255U;
    }

    void line(int x0, int y0, int x1, int y1, Rgb c) {
        const int dx = std::abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
        const int dy = -std::abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
        int err = dx + dy;
        for (int guard = 0; guard < 100000; ++guard) {
            put(x0, y0, c);
            put(x0, y0 + 1, c);  // 2 px thick for small screens
            if (x0 == x1 && y0 == y1) break;
            const int e2 = 2 * err;
            if (e2 >= dy) { err += dy; x0 += sx; }
            if (e2 <= dx) { err += dx; y0 += sy; }
        }
    }

    // Text at scale `s` (glyph 5x7 -> 5s x 7s).
    void text(int x, int y, std::string_view s, Rgb c, int scale) {
        for (const char raw : s) {
            const char ch = static_cast<char>(raw >= 'a' && raw <= 'z' ? raw - 32 : raw);
            const Glyph* glyph = nullptr;
            for (const auto& g : kFont) {
                if (g.c == ch) glyph = &g;
            }
            if (glyph != nullptr) {
                for (int row = 0; row < 7; ++row) {
                    for (int col = 0; col < 5; ++col) {
                        if ((glyph->rows[row] >> (4 - col)) & 1U) {
                            fill(x + col * scale, y + row * scale, x + (col + 1) * scale,
                                 y + (row + 1) * scale, c);
                        }
                    }
                }
            }
            x += 6 * scale;
        }
    }

    [[nodiscard]] ImagePreview take() {
        ImagePreview out;
        out.width = static_cast<std::uint32_t>(w_);
        out.height = static_cast<std::uint32_t>(h_);
        out.rgba8 = std::move(px_);
        return out;
    }

private:
    int w_, h_;
    std::vector<std::uint8_t> px_;
};

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
            canvas.text(right - static_cast<int>(std::string_view{range}.size()) * 6 * scale - 6,
                        panel_top + 6, range, label, scale);
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
