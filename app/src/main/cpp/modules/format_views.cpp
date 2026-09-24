#include "dmcresource/format_views.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <set>

#include "dmcresource/raster_card.h"

namespace dmcresource::views {
namespace {

using raster::Canvas;
using raster::Rgb;

constexpr Rgb kU{120, 200, 255};
constexpr Rgb kV{250, 150, 110};
constexpr Rgb kGood{140, 230, 150};

[[nodiscard]] std::string fmt(const char* format, double a) {
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), format, a);
    return buffer;
}

[[nodiscard]] std::string fmt3(const std::array<float, 3>& v) {
    char buffer[96];
    std::snprintf(buffer, sizeof(buffer), "%.3g, %.3g, %.3g", static_cast<double>(v[0]),
                  static_cast<double>(v[1]), static_cast<double>(v[2]));
    return buffer;
}

void arrow(Canvas& canvas, int x0, int y0, int x1, int y1, Rgb c) {
    canvas.line(x0, y0, x1, y1, c);
    const float dx = static_cast<float>(x1 - x0), dy = static_cast<float>(y1 - y0);
    const float len = std::sqrt(dx * dx + dy * dy);
    if (len < 4.0F) {
        canvas.circle(x1, y1, 3, c);
        return;
    }
    const float ux = dx / len, uy = dy / len;
    const float head = std::min(14.0F, len * 0.4F);
    for (const float side : {-1.0F, 1.0F}) {
        const int hx = x1 - static_cast<int>(ux * head - side * uy * head * 0.5F);
        const int hy = y1 - static_cast<int>(uy * head + side * ux * head * 0.5F);
        canvas.line(x1, y1, hx, hy, c);
    }
}

[[nodiscard]] const char* scroll_type_label(std::uint8_t type) noexcept {
    switch (type) {
    case 0U: return "STEPPED";
    case 1U: return "LINEAR";
    case 2U: return "EASED RATE";
    case 3U: return "EASED TIME";
    case 4U: return "PING-PONG";
    case 5U: return "PING-PONG EASED";
    case 10U: return "FACING";
    default: return "UNKNOWN";
    }
}

}  // namespace

ImagePreview render_tsc_view(const std::vector<motion::ScrollRecord>& records) {
    Canvas canvas{kViewWidth, kViewHeight};
    const int scale = raster::card_scale(kViewWidth);
    const int small = std::max(1, scale - 1);
    canvas.text(10, 8, "TSC TEXTURE SCROLL  " + std::to_string(records.size()) + " RECORDS",
                raster::kAccent, scale);
    canvas.text(10, 8 + 9 * scale, "U / V OFFSET OVER 240 GAME FRAMES (STEP 0x14030C1C0)",
                raster::kDim, small);
    constexpr int kFrames = 240;
    const int top0 = 8 + 9 * scale + 9 * small + 10;
    const int count = std::max<int>(1, static_cast<int>(records.size()));
    const int panel_h = std::min(420, (kViewHeight - top0 - 10) / count - 8);
    if (panel_h < 60) {
        canvas.text(10, top0, "TOO MANY RECORDS FOR THE VIEW", raster::kLabel, scale);
        return canvas.take();
    }
    int top = top0;
    for (const auto& record : records) {
        const int bottom = top + panel_h;
        canvas.fill(10, top, kViewWidth - 10, bottom, raster::kPanel);
        std::string head = "SCROLL " + std::to_string(record.number) + "  TYPE " +
                           std::to_string(record.type) + " " + scroll_type_label(record.type) +
                           "  TEX " +
                           (record.texture < 0 ? std::string{"ALL"} : std::to_string(record.texture));
        canvas.text(16, top + 6, head, raster::kLabel, small);
        char params[160];
        std::snprintf(params, sizeof(params), "DIR %d,%d  TIME %.3g,%.3g  RATE %.3g,%.3g  INTER %.3g,%.3g",
                      record.direction[0], record.direction[1], static_cast<double>(record.time[0]),
                      static_cast<double>(record.time[1]), static_cast<double>(record.rate[0]),
                      static_cast<double>(record.rate[1]), static_cast<double>(record.interval[0]),
                      static_cast<double>(record.interval[1]));
        canvas.text(16, top + 6 + 9 * small, params, raster::kDim, small);

        // Simulate once, keep both offsets per frame.
        std::vector<std::array<float, 2>> offsets(kFrames + 1);
        auto state = motion::start_scroll(record);
        for (int f = 0; f <= kFrames; ++f) {
            offsets[static_cast<std::size_t>(f)] = {
                static_cast<float>(state.output[0] & 0xFFF) / 4096.0F,
                static_cast<float>(state.output[1] & 0xFFF) / 4096.0F};
            motion::step_scroll(record, state, 0.0F);
        }

        // Left: curves. Right: checker strip at 0, 30, 60, 90 frames.
        const int strip = std::min(200, panel_h - 2 * 9 * small - 30);
        const int plot_left = 16;
        const int plot_right = kViewWidth - 20 - (strip > 40 ? strip + 10 : 0);
        const int plot_top = top + 2 * 9 * small + 14;
        const int plot_bottom = bottom - 8;
        canvas.line(plot_left, plot_bottom, plot_right, plot_bottom, raster::kGrid);
        canvas.line(plot_left, plot_top, plot_right, plot_top, raster::kGrid);
        for (int f = 0; f <= kFrames; f += 30) {
            const int x = plot_left + (plot_right - plot_left) * f / kFrames;
            canvas.line(x, plot_top, x, plot_bottom, raster::kGrid);
        }
        for (int axis = 0; axis < 2; ++axis) {
            const Rgb c = axis == 0 ? kU : kV;
            int px = plot_left;
            int py = plot_bottom - static_cast<int>(offsets[0][axis] * (plot_bottom - plot_top));
            for (int f = 1; f <= kFrames; ++f) {
                const int x = plot_left + (plot_right - plot_left) * f / kFrames;
                const int y = plot_bottom -
                              static_cast<int>(offsets[static_cast<std::size_t>(f)][axis] *
                                               (plot_bottom - plot_top));
                // Wrap-around jumps are drawn as breaks.
                if (std::abs(y - py) < (plot_bottom - plot_top) / 2) canvas.line(px, py, x, y, c);
                px = x;
                py = y;
            }
        }
        canvas.text(plot_left + 4, plot_top + 4, "U", kU, small);
        canvas.text(plot_left + 4 + 12 * small, plot_top + 4, "V", kV, small);

        if (strip > 40) {
            const int cell = std::max(4, strip / 8);
            const int sx0 = kViewWidth - 20 - strip;
            const int frames[4] = {0, 30, 60, 90};
            const int tile = strip / 2 - 4;
            for (int k = 0; k < 4; ++k) {
                const int ox = sx0 + (k % 2) * (tile + 8);
                const int oy = plot_top + (k / 2) * (tile + 8);
                const auto& o = offsets[static_cast<std::size_t>(frames[k])];
                const int du = static_cast<int>(o[0] * static_cast<float>(cell * 8));
                const int dv = static_cast<int>(o[1] * static_cast<float>(cell * 8));
                for (int y = 0; y < tile; ++y) {
                    for (int x = 0; x < tile; ++x) {
                        const int u = (x + du) / cell, v = (y + dv) / cell;
                        const bool dark = ((u + v) & 1) != 0;
                        canvas.put(ox + x, oy + y, dark ? Rgb{60, 70, 110} : Rgb{150, 170, 220});
                    }
                }
                canvas.text(ox + 2, oy + 2, std::to_string(frames[k]), raster::kAccent, 1);
            }
        }
        top = bottom + 8;
    }
    return canvas.take();
}

ImagePreview render_clt_view(const std::vector<motion::ClothParams>& blocks) {
    Canvas canvas{kViewWidth, kViewHeight};
    const int scale = raster::card_scale(kViewWidth);
    const int small = std::max(1, scale - 1);
    canvas.text(10, 8, "CLT CHAIN / CLOTH  " + std::to_string(blocks.size()) + " BLOCKS",
                raster::kAccent, scale);
    canvas.text(10, 8 + 9 * scale, "BONE = NODE + BEND AXIS; ARROWS: GRAVITY (GREEN) WIND (BLUE)",
                raster::kDim, small);
    static constexpr const char* kAxes[] = {"X", "Y", "Z", "-X", "-Y", "-Z"};
    const int top0 = 8 + 9 * scale + 9 * small + 10;
    const int count = std::max<int>(1, static_cast<int>(blocks.size()));
    const int columns = count > 4 ? 2 : 1;
    const int rows = (count + columns - 1) / columns;
    const int panel_w = (kViewWidth - 20 - (columns - 1) * 8) / columns;
    const int panel_h = (kViewHeight - top0 - 10) / rows - 8;
    if (panel_h < 80) {
        canvas.text(10, top0, "TOO MANY BLOCKS FOR THE VIEW", raster::kLabel, scale);
        return canvas.take();
    }
    for (int i = 0; i < static_cast<int>(blocks.size()); ++i) {
        const auto& p = blocks[static_cast<std::size_t>(i)];
        const int x0 = 10 + (i % columns) * (panel_w + 8);
        const int y0 = top0 + (i / columns) * (panel_h + 8);
        const int x1 = x0 + panel_w, y1 = y0 + panel_h;
        canvas.fill(x0, y0, x1, y1, raster::kPanel);
        canvas.text(x0 + 6, y0 + 6,
                    "CLOTH " + std::to_string(i) + "  " + std::to_string(p.bones.size()) + " BONES",
                    raster::kLabel, small);

        // Parameters (right half).
        const int tx = x0 + panel_w / 2;
        int ty = y0 + 6 + 9 * small + 4;
        const auto line = [&](const std::string& s) {
            if (ty + 9 * small > y1) return;
            canvas.text(tx, ty, s, raster::kDim, small);
            ty += 9 * small;
        };
        line("GRAVITY " + fmt3(p.gravity));
        line("WIND " + fmt3(p.wind) + (p.wind_local ? " LOCAL" : ""));
        line("WIND PARENT " + std::to_string(p.wind_parent) + " TYPE " + std::to_string(p.wind_type));
        line("STIFFNESS " + fmt("%.3g", p.stiffness));
        line("SPRING " + fmt("%.3g", p.spring_force));
        line("MAX SPEED " + fmt("%.3g", p.max_speed));
        line("DAMPING " + fmt("%.3g", p.damping));
        line(std::string{"LIMIT LENGTH "} + (p.limit_length ? "ON" : "OFF"));
        if (p.floor_level > -999999.0F) line("FLOOR " + fmt("%.3g", p.floor_level));

        // Strands (left half): a bone whose node is not the previous node + 1
        // starts a new strand (its parent, node - 1, is the fixed root the
        // solver never moves). Strands hang side by side along gravity (x, y).
        std::vector<std::vector<motion::ClothBone>> strands;
        for (std::size_t b = 0U; b < p.bones.size(); ++b) {
            if (b == 0U || p.bones[b].node != p.bones[b - 1U].node + 1U) strands.emplace_back();
            strands.back().push_back(p.bones[b]);
        }
        const int area_l = x0 + 12, area_r = tx - 12;
        const int cy0 = y0 + 9 * small + 26;
        const int chain_h = y1 - cy0 - 50;
        const float gx = p.gravity[0], gy = p.gravity[1];
        const float gl = std::sqrt(gx * gx + gy * gy);
        const float dx = gl > 1.0e-6F ? gx / gl : 0.0F;
        const float dy = gl > 1.0e-6F ? -gy / gl : 1.0F;  // screen y grows down
        std::size_t longest = 1U;
        for (const auto& strand : strands) longest = std::max(longest, strand.size());
        const float step = static_cast<float>(chain_h) / static_cast<float>(longest);
        const int ns = std::max<int>(1, static_cast<int>(strands.size()));
        const int lane = (area_r - area_l) / ns;
        for (int k = 0; k < static_cast<int>(strands.size()); ++k) {
            const auto& strand = strands[static_cast<std::size_t>(k)];
            const int cx = area_l + lane * k + lane / 3;
            canvas.fill(cx - 8, cy0 - 4, cx + 8, cy0, raster::kGrid);
            canvas.text(cx - 8, cy0 - 4 - 9 * small,
                        std::to_string(strand.front().node == 0U ? 0U : strand.front().node - 1U),
                        raster::kDim, small);
            int px = cx, py = cy0;
            for (std::size_t b = 0U; b < strand.size(); ++b) {
                const auto& bone = strand[b];
                const auto f = static_cast<float>(b + 1U);
                const int x = cx + static_cast<int>(dx * step * f * 0.6F);
                const int y = cy0 + static_cast<int>(dy * step * f);
                canvas.line(px, py, x, y, raster::kLabel);
                canvas.circle(x, y, 4, raster::kAccent);
                canvas.text(x + 8, y - 3 * small,
                            std::to_string(bone.node) + " " + kAxes[bone.axis < 6U ? bone.axis : 1U],
                            raster::kLabel, small);
                px = x;
                py = y;
            }
        }
        line(std::to_string(strands.size()) + " STRANDS");
        // Force arrows (bottom-left corner of the chain area).
        const int ax = tx + 40, ay = y1 - 40;
        const auto force = [&](const std::array<float, 3>& v, Rgb c) {
            const float l = std::sqrt(v[0] * v[0] + v[1] * v[1]);
            if (l < 1.0e-6F) return;
            const float k = 26.0F / l;
            arrow(canvas, ax, ay, ax + static_cast<int>(v[0] * k), ay - static_cast<int>(v[1] * k), c);
        };
        force(p.gravity, kGood);
        force(p.wind, kU);
    }
    return canvas.take();
}

ImagePreview render_motion_script_view(const motion::MotionScriptFile& file) {
    Canvas canvas{kViewWidth, kViewHeight};
    const int scale = raster::card_scale(kViewWidth);
    const int small = std::max(1, scale - 1);
    std::size_t total = 0U, max_scripts = 1U;
    std::array<std::uint32_t, 64> opcodes{};
    struct Row final {
        std::size_t scripts{};
        std::set<int> states;
        std::set<int> played_banks;
        std::size_t waits{}, loops{}, hand_overs{};
        int longest{};
    };
    std::vector<Row> rows(file.bank_count());
    for (std::size_t bank = 0U; bank < file.bank_count(); ++bank) {
        auto& row = rows[bank];
        row.scripts = file.script_count(bank);
        total += row.scripts;
        max_scripts = std::max(max_scripts, row.scripts);
        for (std::size_t i = 0U; i < row.scripts; ++i) {
            const auto s = file.summarize(bank, i);
            if (!s) continue;
            for (const auto& key : s->states) row.states.insert(key.state);
            row.played_banks.insert(s->play_bank);
            row.waits += s->waits;
            row.loops += s->loops ? 1U : 0U;
            row.hand_overs += s->hands_over ? 1U : 0U;
            row.longest = std::max<int>(row.longest, s->last_frame);
            for (std::size_t op = 0U; op < opcodes.size(); ++op) opcodes[op] += s->opcodes[op];
        }
    }
    canvas.text(10, 8, "PLAYER MOTION SCRIPT  " + std::to_string(file.bank_count()) + " BANKS  " +
                           std::to_string(total) + " SCRIPTS",
                raster::kAccent, scale);
    canvas.text(10, 8 + 9 * scale,
                "BANK N = PL000_00_N.PAC; STATES = WEAPON ATTACH (OPCODE 3 BYTE 2)", raster::kDim,
                small);
    const int top = 8 + 9 * scale + 9 * small + 12;
    const int footer = 9 * small * 4 + 20;
    const int avail = kViewHeight - top - footer;
    const int row_h = std::clamp(avail / std::max<int>(1, static_cast<int>(rows.size())), 9 * small + 2,
                                 9 * scale + 8);
    const int text_scale = row_h >= 9 * scale + 2 ? scale : small;
    const int bar_x = 10 + 6 * text_scale * 9;
    const int bar_w = 260;
    for (std::size_t bank = 0U; bank < rows.size(); ++bank) {
        const int y = top + static_cast<int>(bank) * row_h;
        if (y + row_h > kViewHeight - footer) break;
        if (bank % 2U == 0U) canvas.fill(10, y, kViewWidth - 10, y + row_h, raster::kPanel);
        const auto& row = rows[bank];
        char label[32];
        std::snprintf(label, sizeof(label), "%2zu %3zu", bank, row.scripts);
        canvas.text(14, y + 1, label, raster::kLabel, text_scale);
        const int w = static_cast<int>(bar_w * row.scripts / max_scripts);
        canvas.fill(bar_x, y + 3, bar_x + w, y + row_h - 3, Rgb{90, 120, 190});
        std::string states;
        for (const int s : row.states) {
            if (!states.empty()) states += ' ';
            states += std::to_string(s);
        }
        std::string tail = "WAITS " + std::to_string(row.waits) + " LAST F" +
                           std::to_string(row.longest);
        if (row.loops != 0U) tail += " LOOP " + std::to_string(row.loops);
        if (!states.empty()) tail += "  ST " + states;
        const auto room = static_cast<std::size_t>(
            std::max(8, (kViewWidth - (bar_x + bar_w + 10) - 10) / (6 * small)));
        if (tail.size() > room) tail = tail.substr(0U, room - 2U) + "..";
        canvas.text(bar_x + bar_w + 10, y + 1, tail, row.states.empty() ? raster::kDim : kGood,
                    small);
    }
    // Opcode histogram.
    int fy = kViewHeight - footer + 6;
    canvas.text(10, fy, "OPCODES (COUNT)", raster::kLabel, small);
    fy += 9 * small;
    std::string line;
    for (std::size_t op = 0U; op < opcodes.size(); ++op) {
        if (opcodes[op] == 0U) continue;
        std::string item = std::to_string(op) + ":" + std::to_string(opcodes[op]) + " ";
        if (Canvas::text_width(line + item, small) > kViewWidth - 30) {
            canvas.text(10, fy, line, raster::kDim, small);
            fy += 9 * small;
            line.clear();
        }
        line += item;
    }
    if (!line.empty()) canvas.text(10, fy, line, raster::kDim, small);
    return canvas.take();
}

BinaryProfile profile_binary(std::span<const std::uint8_t> bytes) {
    BinaryProfile out;
    out.size = bytes.size();
    out.histogram.assign(256U, 0U);
    std::size_t zeros = 0U, printable = 0U;
    for (const auto b : bytes) {
        ++out.histogram[b];
        zeros += b == 0U ? 1U : 0U;
        printable += (b >= 0x20U && b < 0x7FU) || b == '\n' || b == '\r' || b == '\t' ? 1U : 0U;
    }
    const auto entropy_of = [](std::span<const std::uint8_t> part) {
        if (part.empty()) return 0.0;
        std::array<std::uint32_t, 256> h{};
        for (const auto b : part) ++h[b];
        double e = 0.0;
        for (const auto c : h) {
            if (c == 0U) continue;
            const double p = static_cast<double>(c) / static_cast<double>(part.size());
            e -= p * std::log2(p);
        }
        return e;
    };
    out.entropy = entropy_of(bytes);
    if (!bytes.empty()) {
        out.zero_ratio = static_cast<double>(zeros) / static_cast<double>(bytes.size());
        out.printable_ratio = static_cast<double>(printable) / static_cast<double>(bytes.size());
    }
    // ~512 blocks at most, at least 256 bytes each.
    out.block_size = std::max<std::size_t>(256U, (bytes.size() + 511U) / 512U);
    for (std::size_t o = 0U; o < bytes.size(); o += out.block_size) {
        const auto n = std::min(out.block_size, bytes.size() - o);
        out.block_entropy.push_back(static_cast<float>(entropy_of(bytes.subspan(o, n))));
    }
    const auto u32 = [&bytes](std::size_t o) -> std::uint32_t {
        if (o + 4U > bytes.size()) return 0U;
        return static_cast<std::uint32_t>(bytes[o]) | (static_cast<std::uint32_t>(bytes[o + 1U]) << 8U) |
               (static_cast<std::uint32_t>(bytes[o + 2U]) << 16U) |
               (static_cast<std::uint32_t>(bytes[o + 3U]) << 24U);
    };
    out.u32_0 = u32(0U);
    out.u32_4 = u32(4U);
    out.u32_8 = u32(8U);
    out.u32_12 = u32(12U);
    if (bytes.size() >= 4U) {
        bool text = true;
        for (std::size_t i = 0U; i < 4U; ++i) {
            const auto b = bytes[i];
            const bool word = (b >= '0' && b <= '9') || (b >= 'A' && b <= 'Z') ||
                              (b >= 'a' && b <= 'z') || b == ' ' || b == '_' || b == '.';
            if (!(word || (b == 0U && i == 3U))) text = false;
        }
        if (text) {
            for (std::size_t i = 0U; i < 4U && bytes[i] != 0U; ++i) {
                out.fourcc.push_back(static_cast<char>(bytes[i]));
            }
        }
    }
    // Ascending u32 offsets from the start (or after a count at +0).
    for (std::size_t start : {std::size_t{0U}, std::size_t{4U}, std::size_t{8U}}) {
        std::size_t entries = 0U;
        std::uint32_t previous = 0U;
        for (std::size_t o = start; o + 4U <= bytes.size() && entries < 4096U; o += 4U) {
            const auto v = u32(o);
            if (v == 0U && entries == 0U) break;
            if (v < previous || v >= bytes.size() || v < o + 4U) break;
            previous = v;
            ++entries;
        }
        out.offset_table_entries = std::max(out.offset_table_entries, entries >= 2U ? entries : 0U);
    }
    // Float32 words.
    std::size_t nonzero_words = 0U, float_words = 0U;
    for (std::size_t o = 0U; o + 4U <= bytes.size(); o += 4U) {
        const auto v = u32(o);
        if (v == 0U) continue;
        ++nonzero_words;
        float f;
        std::memcpy(&f, &v, sizeof(f));
        const float a = std::fabs(f);
        if (std::isfinite(f) && a >= 1.0e-4F && a <= 1.0e5F) ++float_words;
    }
    if (nonzero_words != 0U) {
        out.float_ratio = static_cast<double>(float_words) / static_cast<double>(nonzero_words);
    }
    // Record stride.
    {
        const auto sample = bytes.subspan(0U, std::min<std::size_t>(bytes.size(), 65536U));
        std::size_t nonzero = 0U;
        for (const auto b : sample) nonzero += b != 0U ? 1U : 0U;
        if (nonzero >= 64U) {
            for (std::size_t stride = 4U; stride <= 512U && stride * 4U <= sample.size(); stride += 4U) {
                std::size_t hits = 0U, tests = 0U;
                for (std::size_t i = 0U; i + stride < sample.size(); ++i) {
                    if (sample[i] == 0U) continue;
                    ++tests;
                    hits += sample[i] == sample[i + stride] ? 1U : 0U;
                }
                if (tests == 0U) continue;
                const double score = static_cast<double>(hits) / static_cast<double>(tests);
                if (score > out.stride_score + 0.02) {
                    out.stride_score = score;
                    out.stride = stride;
                }
            }
            if (out.stride_score < 0.3) {
                out.stride = 0U;
                out.stride_score = 0.0;
            }
        }
    }
    // Printable runs.
    std::size_t run = 0U;
    for (std::size_t i = 0U; i <= bytes.size() && out.strings.size() < 32U; ++i) {
        const bool p = i < bytes.size() && bytes[i] >= 0x20U && bytes[i] < 0x7FU;
        if (p) {
            ++run;
            continue;
        }
        if (run >= 5U) {
            const auto begin = i - run;
            std::string s(reinterpret_cast<const char*>(bytes.data() + begin), std::min<std::size_t>(run, 64U));
            out.strings.push_back(std::move(s));
            out.string_offsets.push_back(begin);
        }
        run = 0U;
    }
    return out;
}

void append_binary_inspection(InspectionDocument& inspection, const BinaryProfile& profile) {
    auto& root = inspection.root;
    const auto add = [&root](const char* key, std::string value) {
        root.properties.push_back({key, std::move(value), EvidenceLevel::DataConfirmed});
    };
    add("Bytes", std::to_string(profile.size));
    add("Entropy", fmt("%.3f bits/byte", profile.entropy));
    add("Zero bytes", fmt("%.1f%%", profile.zero_ratio * 100.0));
    add("Printable", fmt("%.1f%%", profile.printable_ratio * 100.0));
    if (!profile.fourcc.empty()) add("FourCC", "\"" + profile.fourcc + "\"");
    char words[96];
    std::snprintf(words, sizeof(words), "%08X %08X %08X %08X", profile.u32_0, profile.u32_4,
                  profile.u32_8, profile.u32_12);
    add("u32 +0..+12", words);
    if (profile.offset_table_entries != 0U) {
        add("Offset table", std::to_string(profile.offset_table_entries) +
                                " ascending u32 offsets inside the file");
    }
    if (profile.float_ratio > 0.0) {
        add("Float32 words", fmt("%.0f%% of non-zero u32", profile.float_ratio * 100.0));
    }
    if (profile.stride != 0U) {
        add("Record stride", std::to_string(profile.stride) + " bytes (" +
                                 fmt("%.0f%% repeat)", profile.stride_score * 100.0));
    }
    if (!profile.strings.empty()) {
        InspectionNode strings;
        strings.id = "strings";
        strings.title = "Strings (" + std::to_string(profile.strings.size()) + ")";
        strings.kind = InspectionKind::Collection;
        for (std::size_t i = 0U; i < profile.strings.size(); ++i) {
            char offset[24];
            std::snprintf(offset, sizeof(offset), "+%06zX", profile.string_offsets[i]);
            strings.properties.push_back({offset, profile.strings[i], EvidenceLevel::DataConfirmed});
        }
        root.children.push_back(std::move(strings));
    }
}

ImagePreview render_binary_view(const InspectionDocument& inspection, std::string_view detail,
                                const BinaryProfile& profile, std::span<const std::uint8_t> bytes) {
    Canvas canvas{kViewWidth, kViewHeight};
    const int scale = raster::card_scale(kViewWidth);
    const int small = std::max(1, scale - 1);
    // Entropy strip: one column per block, height = entropy / 8.
    int y = 8;
    canvas.text(10, y, "ENTROPY PER " + std::to_string(profile.block_size) + " BYTES", raster::kLabel, small);
    y += 9 * small + 2;
    const int strip_h = 90;
    canvas.fill(10, y, kViewWidth - 10, y + strip_h, raster::kPanel);
    const int n = static_cast<int>(profile.block_entropy.size());
    for (int i = 0; i < n; ++i) {
        const float e = profile.block_entropy[static_cast<std::size_t>(i)] / 8.0F;
        const int x0 = 10 + (kViewWidth - 20) * i / std::max(1, n);
        const int x1 = std::max(x0 + 1, 10 + (kViewWidth - 20) * (i + 1) / std::max(1, n));
        const Rgb c{static_cast<std::uint8_t>(60 + 190 * e), static_cast<std::uint8_t>(200 - 120 * e),
                    static_cast<std::uint8_t>(120 + 60 * (1.0F - e))};
        canvas.fill(x0, y + strip_h - static_cast<int>(e * (strip_h - 4)), x1, y + strip_h, c);
    }
    y += strip_h + 8;
    // Byte histogram (log scale).
    canvas.text(10, y, "BYTE HISTOGRAM 00..FF (LOG)", raster::kLabel, small);
    y += 9 * small + 2;
    const int hist_h = 80;
    canvas.fill(10, y, kViewWidth - 10, y + hist_h, raster::kPanel);
    std::uint32_t peak = 1U;
    for (const auto c : profile.histogram) peak = std::max(peak, c);
    const double lp = std::log1p(static_cast<double>(peak));
    for (int b = 0; b < 256 && b < static_cast<int>(profile.histogram.size()); ++b) {
        const double v = std::log1p(static_cast<double>(profile.histogram[static_cast<std::size_t>(b)])) / lp;
        const int x0 = 10 + (kViewWidth - 20) * b / 256;
        const int x1 = 10 + (kViewWidth - 20) * (b + 1) / 256;
        const Rgb c = (b >= 0x20 && b < 0x7F) ? kGood : raster::kDim;
        canvas.fill(x0, y + hist_h - static_cast<int>(v * (hist_h - 4)), x1, y + hist_h, c);
    }
    y += hist_h + 10;
    (void)raster::draw_info_card(canvas, y, inspection, detail, bytes);
    return canvas.take();
}

}  // namespace dmcresource::views
