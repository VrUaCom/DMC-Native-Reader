#include "dmcresource/raster_card.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>

namespace dmcresource::raster {
namespace {

// 5x7 glyphs, one row per byte, bit 4 = left column.
struct Glyph final {
    char c;
    std::array<std::uint8_t, 7> rows;
};

constexpr std::array<Glyph, 69> kFont{{
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
    {'_', {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1F}}, {',', {0x00, 0x00, 0x00, 0x00, 0x0C, 0x04, 0x08}},
    {'=', {0x00, 0x00, 0x1F, 0x00, 0x1F, 0x00, 0x00}}, {'|', {0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04}},
    {'#', {0x0A, 0x0A, 0x1F, 0x0A, 0x1F, 0x0A, 0x0A}}, {'%', {0x18, 0x19, 0x02, 0x04, 0x08, 0x13, 0x03}},
    {'[', {0x0E, 0x08, 0x08, 0x08, 0x08, 0x08, 0x0E}}, {']', {0x0E, 0x02, 0x02, 0x02, 0x02, 0x02, 0x0E}},
    {'<', {0x02, 0x04, 0x08, 0x10, 0x08, 0x04, 0x02}}, {'>', {0x08, 0x04, 0x02, 0x01, 0x02, 0x04, 0x08}},
    {'!', {0x04, 0x04, 0x04, 0x04, 0x04, 0x00, 0x04}}, {'?', {0x0E, 0x11, 0x01, 0x02, 0x04, 0x00, 0x04}},
    {'\'', {0x0C, 0x04, 0x08, 0x00, 0x00, 0x00, 0x00}}, {'"', {0x0A, 0x0A, 0x0A, 0x00, 0x00, 0x00, 0x00}},
    {'*', {0x00, 0x04, 0x15, 0x0E, 0x15, 0x04, 0x00}}, {'&', {0x0C, 0x12, 0x14, 0x08, 0x15, 0x12, 0x0D}},
    {'@', {0x0E, 0x11, 0x01, 0x0D, 0x15, 0x15, 0x0E}}, {'$', {0x04, 0x0F, 0x14, 0x0E, 0x05, 0x1E, 0x04}},
    {';', {0x00, 0x0C, 0x0C, 0x00, 0x0C, 0x04, 0x08}}, {'{', {0x02, 0x04, 0x04, 0x08, 0x04, 0x04, 0x02}},
    {'}', {0x08, 0x04, 0x04, 0x02, 0x04, 0x04, 0x08}}, {'~', {0x00, 0x00, 0x08, 0x15, 0x02, 0x00, 0x00}},
    {'^', {0x04, 0x0A, 0x11, 0x00, 0x00, 0x00, 0x00}}, {'\\', {0x10, 0x10, 0x08, 0x04, 0x02, 0x01, 0x01}},
    {'x', {0x00, 0x00, 0x11, 0x0A, 0x04, 0x0A, 0x11}},
}};

[[nodiscard]] const Glyph* glyph_for(char raw, char previous) noexcept {
    if (raw == 'x' && previous == '0') return &kFont.back();  // keep "0x" readable
    const char c = static_cast<char>(raw >= 'a' && raw <= 'z' ? raw - 32 : raw);
    for (const auto& g : kFont) {
        if (g.c == c) return &g;
    }
    return nullptr;
}

constexpr std::array<Rgb, 5> kEvidence{{
    {130, 134, 150},  // unknown / recognized
    {120, 200, 255},
    {140, 230, 150},
    {250, 190, 90},
    {240, 120, 120},
}};

}  // namespace

Canvas::Canvas(int width, int height)
    : w_(width), h_(height), px_(static_cast<std::size_t>(width) * height * 4U) {
    fill(0, 0, w_, h_, kBackground);
}

void Canvas::fill(int x0, int y0, int x1, int y1, Rgb c) {
    for (int y = std::max(0, y0); y < std::min(h_, y1); ++y) {
        for (int x = std::max(0, x0); x < std::min(w_, x1); ++x) put(x, y, c);
    }
}

void Canvas::put(int x, int y, Rgb c) {
    if (x < 0 || y < 0 || x >= w_ || y >= h_) return;
    const auto o = (static_cast<std::size_t>(y) * w_ + x) * 4U;
    px_[o] = c.r;
    px_[o + 1U] = c.g;
    px_[o + 2U] = c.b;
    px_[o + 3U] = 255U;
}

void Canvas::line(int x0, int y0, int x1, int y1, Rgb c) {
    const int dx = std::abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    const int dy = -std::abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (int guard = 0; guard < 200000; ++guard) {
        put(x0, y0, c);
        put(x0, y0 + 1, c);
        if (x0 == x1 && y0 == y1) break;
        const int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

void Canvas::circle(int cx, int cy, int radius, Rgb c) {
    for (int y = -radius; y <= radius; ++y) {
        for (int x = -radius; x <= radius; ++x) {
            if (x * x + y * y <= radius * radius) put(cx + x, cy + y, c);
        }
    }
}

int Canvas::text(int x, int y, std::string_view s, Rgb c, int scale) {
    char previous = ' ';
    for (const char raw : s) {
        if (x > w_) break;
        const auto* g = glyph_for(raw, previous);
        previous = raw;
        if (g == nullptr) {
            // Unknown glyph: small box.
            fill(x + scale, y + 2 * scale, x + 4 * scale, y + 6 * scale, c);
        } else {
            for (int row = 0; row < 7; ++row) {
                for (int col = 0; col < 5; ++col) {
                    if ((g->rows[row] >> (4 - col)) & 1U) {
                        fill(x + col * scale, y + row * scale, x + (col + 1) * scale,
                             y + (row + 1) * scale, c);
                    }
                }
            }
        }
        x += 6 * scale;
    }
    return x;
}

ImagePreview Canvas::take() {
    ImagePreview out;
    out.width = static_cast<std::uint32_t>(w_);
    out.height = static_cast<std::uint32_t>(h_);
    out.rgba8 = std::move(px_);
    return out;
}

int card_scale(int width) noexcept { return std::max(1, width / 360); }

namespace {

// Writes `text` wrapped to the canvas width; returns the y after it.
int wrapped(Canvas& canvas, int x, int y, std::string_view text, Rgb c, int scale, int max_lines) {
    const int line_h = 9 * scale;
    const int chars = std::max(8, (canvas.width() - x - 8) / (6 * scale));
    int lines = 0;
    while (!text.empty() && lines < max_lines && y + line_h <= canvas.height()) {
        const auto take = std::min<std::size_t>(text.size(), static_cast<std::size_t>(chars));
        canvas.text(x, y, text.substr(0U, take), c, scale);
        text.remove_prefix(take);
        y += line_h;
        ++lines;
    }
    return y;
}

void tree(Canvas& canvas, int& y, const InspectionNode& node, int depth, int scale, int bottom,
          int& budget) {
    const int line_h = 9 * scale;
    for (const auto& child : node.children) {
        if (budget <= 0 || y + line_h > bottom) return;
        const int x = 10 + depth * 4 * 6 * scale / 2;
        std::string head = child.title.empty() ? child.id : child.title;
        canvas.text(x, y, head, kAccent, scale);
        y += line_h;
        --budget;
        for (const auto& p : child.properties) {
            if (budget <= 0 || y + line_h > bottom) return;
            const auto evidence = static_cast<std::size_t>(p.evidence);
            const Rgb c = evidence < kEvidence.size() ? kEvidence[evidence] : kLabel;
            y = wrapped(canvas, x + 2 * 6 * scale, y, p.key + ": " + p.value, c, scale, 2);
            --budget;
        }
        tree(canvas, y, child, depth + 1, scale, bottom, budget);
    }
}

}  // namespace

int draw_hex_dump(Canvas& canvas, int y, std::span<const std::uint8_t> bytes,
                  std::size_t first_offset) {
    const int scale = std::max(1, card_scale(canvas.width()) - 1);
    const int line_h = 9 * scale;
    if (y + line_h * 2 > canvas.height()) return y;
    canvas.fill(0, y, canvas.width(), y + 2, kGrid);
    y += 6;
    char row[128];
    for (std::size_t o = 0U; o < bytes.size() && y + line_h <= canvas.height(); o += 16U) {
        int n = std::snprintf(row, sizeof(row), "%06zX ", first_offset + o);
        for (std::size_t k = 0U; k < 16U; ++k) {
            if (o + k < bytes.size()) {
                n += std::snprintf(row + n, sizeof(row) - static_cast<std::size_t>(n), "%02X ",
                                   bytes[o + k]);
            } else {
                n += std::snprintf(row + n, sizeof(row) - static_cast<std::size_t>(n), "   ");
            }
        }
        std::string ascii;
        for (std::size_t k = 0U; k < 16U && o + k < bytes.size(); ++k) {
            const auto b = bytes[o + k];
            ascii.push_back(b >= 0x20U && b < 0x7FU ? static_cast<char>(b) : '.');
        }
        const int x = canvas.text(6, y, std::string_view{row, static_cast<std::size_t>(n)}, kDim, scale);
        canvas.text(x, y, ascii, kLabel, scale);
        y += line_h;
    }
    return y;
}

int draw_info_card(Canvas& canvas, int y, const InspectionDocument& inspection,
                   std::string_view detail, std::span<const std::uint8_t> bytes) {
    const int scale = card_scale(canvas.width());
    const int line_h = 9 * scale;
    // Leave room for at least a few hex rows.
    const int hex_reserve = bytes.empty() ? 0 : 10 * 9 * std::max(1, scale - 1) + 12;
    const int bottom = canvas.height() - hex_reserve;

    std::string title = inspection.format.empty() ? std::string{"RESOURCE"} : inspection.format;
    if (!inspection.root.title.empty() && inspection.root.title != title) {
        title += "  " + inspection.root.title;
    }
    char size[48];
    std::snprintf(size, sizeof(size), "  %zu BYTES", bytes.size());
    canvas.text(10, y, title + size, kAccent, scale);
    y += line_h + 4;

    // Detail lines (module summary).
    std::size_t start = 0U;
    int detail_lines = 0;
    while (start < detail.size() && detail_lines < 8 && y + line_h <= bottom) {
        auto end = detail.find('\n', start);
        if (end == std::string_view::npos) end = detail.size();
        y = wrapped(canvas, 10, y, detail.substr(start, end - start), kDim, scale, 2);
        start = end + 1U;
        ++detail_lines;
    }
    y += 4;
    for (const auto& p : inspection.root.properties) {
        if (y + line_h > bottom) break;
        const auto evidence = static_cast<std::size_t>(p.evidence);
        const Rgb c = evidence < kEvidence.size() ? kEvidence[evidence] : kLabel;
        y = wrapped(canvas, 10, y, p.key + ": " + p.value, c, scale, 2);
    }
    y += 4;
    int budget = 400;
    tree(canvas, y, inspection.root, 0, scale, bottom, budget);
    if (!bytes.empty()) y = draw_hex_dump(canvas, y + 6, bytes);
    return y;
}

ImagePreview render_info_card(const InspectionDocument& inspection, std::string_view detail,
                              std::span<const std::uint8_t> bytes) {
    Canvas canvas{1080, 1440};
    (void)draw_info_card(canvas, 10, inspection, detail, bytes);
    return canvas.take();
}

}  // namespace dmcresource::raster
