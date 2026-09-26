#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "dmcresource/image_preview.h"
#include "dmcresource/inspection_document.h"

// Stand-alone 2D views for resources without geometry or pixels: a small
// software canvas with a 5x7 bitmap font, an information card that prints
// everything the inspection knows plus a hex dump of the bytes, and helpers
// the format views (MOT curves, TSC scrolls, CLT chains, motion scripts) share.
namespace dmcresource::raster {

struct Rgb final {
    std::uint8_t r{}, g{}, b{};
};

inline constexpr Rgb kBackground{18, 18, 22};
inline constexpr Rgb kPanel{28, 29, 36};
inline constexpr Rgb kGrid{52, 54, 66};
inline constexpr Rgb kLabel{200, 202, 214};
inline constexpr Rgb kDim{130, 134, 150};
inline constexpr Rgb kAccent{250, 190, 90};

class Canvas final {
public:
    Canvas(int width, int height);

    [[nodiscard]] int width() const noexcept { return w_; }
    [[nodiscard]] int height() const noexcept { return h_; }

    void fill(int x0, int y0, int x1, int y1, Rgb c);
    void put(int x, int y, Rgb c);
    void line(int x0, int y0, int x1, int y1, Rgb c);
    void circle(int cx, int cy, int radius, Rgb c);
    // 5x7 glyphs at `scale`; lower case prints as upper case; unknown glyphs
    // print as a box. Returns the x after the text.
    int text(int x, int y, std::string_view s, Rgb c, int scale);
    [[nodiscard]] static int text_width(std::string_view s, int scale) noexcept {
        return static_cast<int>(s.size()) * 6 * scale;
    }

    [[nodiscard]] ImagePreview take();

private:
    int w_, h_;
    std::vector<std::uint8_t> px_;
};

// Card scale for a canvas width (1080 px -> 3).
[[nodiscard]] int card_scale(int width) noexcept;

// Prints title, detail lines, root properties and the inspection tree from
// `y`, then fills the rest with a hex + ASCII dump of `bytes`. Returns the y
// after the last line drawn.
// `hex` false: the byte count still shows, the dump is left to the caller.
int draw_info_card(Canvas& canvas, int y, const InspectionDocument& inspection,
                   std::string_view detail, std::span<const std::uint8_t> bytes, bool hex = true);

// Hex dump rows (offset, 16 bytes, ASCII) from `y` to the canvas bottom.
int draw_hex_dump(Canvas& canvas, int y, std::span<const std::uint8_t> bytes,
                  std::size_t first_offset = 0U);

// Whole-image information card (1080 x 1440).
[[nodiscard]] ImagePreview render_info_card(const InspectionDocument& inspection,
                                            std::string_view detail,
                                            std::span<const std::uint8_t> bytes);

}  // namespace dmcresource::raster
