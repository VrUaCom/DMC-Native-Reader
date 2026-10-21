#include "dmcresource/stage_markers.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <string_view>
#include <utility>

#include "dmcresource/raster_card.h"

namespace dmcresource::stage_markers {
namespace {

using stage_config::Kind;
using stage_config::Vec3;

[[nodiscard]] std::string number(float value) {
    char text[32];
    std::snprintf(text, sizeof text, "%.2f", static_cast<double>(value));
    return text;
}

[[nodiscard]] std::string point_text(const Vec3& p) {
    return number(p.x) + ", " + number(p.y) + ", " + number(p.z);
}

[[nodiscard]] std::string hex(std::uint64_t value) {
    char text[24];
    std::snprintf(text, sizeof text, "0x%llX", static_cast<unsigned long long>(value));
    return text;
}

[[nodiscard]] Marker make(Kind kind, std::size_t record, Shape shape, std::uint64_t offset) {
    Marker marker;
    marker.source = kind;
    marker.record = record;
    marker.shape = shape;
    marker.offset = offset;
    marker.color = kind_color(kind);
    marker.label = std::string{stage_config::kind_name(kind)} + " " + std::to_string(record);
    return marker;
}

void area_fields(Marker& marker, const stage_config::Quad& quad, float extent) {
    marker.points.assign(quad.corners.begin(), quad.corners.end());
    marker.height = extent;
    marker.fields.emplace_back("Floor height", number(quad.corners[0].y));
    marker.fields.emplace_back("Reaches up", number(extent));
    for (std::size_t corner = 0U; corner < 4U; ++corner) {
        marker.fields.emplace_back("Corner " + std::to_string(corner + 1U), point_text(quad.corners[corner]));
    }
    if (!quad.level) marker.fields.emplace_back("Level", "the corners are not on one level");
}

void record_field(Marker& marker, std::uint64_t offset, std::string_view file) {
    marker.fields.emplace_back("Record", "at " + hex(offset) + " in the " + std::string{file} + " file");
}

}  // namespace

std::string Marker::description() const {
    std::string text;
    for (const auto& [name, value] : fields) {
        if (!text.empty()) text += '\n';
        text += name + ": " + value;
    }
    return text;
}

Rgb kind_color(Kind kind) noexcept {
    switch (kind) {
    case Kind::Pos: return {90, 220, 120};
    case Kind::Eve: return {250, 160, 60};
    case Kind::Cam: return {80, 200, 240};
    case Kind::Itm: return {245, 225, 80};
    case Kind::Ste: return {225, 110, 230};
    case Kind::Est: return {200, 202, 214};
    case Kind::Sef: return {200, 202, 214};
    }
    return {200, 202, 214};
}

const char* kind_meaning(Kind kind) noexcept {
    switch (kind) {
    case Kind::Pos: return "spawn points";
    case Kind::Eve: return "event zones";
    case Kind::Cam: return "camera zones and paths";
    case Kind::Itm: return "item placements";
    case Kind::Ste: return "stage elements";
    case Kind::Est: return "enemy set programs by difficulty";
    case Kind::Sef: return "stage effect sections";
    }
    return "";
}

std::vector<Marker> read_markers(Kind kind, std::span<const std::uint8_t> bytes) {
    std::vector<Marker> markers;
    switch (kind) {
    case Kind::Pos: {
        const auto read = stage_config::read_pos(bytes);
        if (!read.recognized) break;
        for (std::size_t i = 0; i < read.document.records.size(); ++i) {
            const auto& r = read.document.records[i];
            auto m = make(kind, i, Shape::Point, r.offset);
            m.points = {r.position};
            m.heading_degrees = r.heading_degrees;
            m.title = "Spawn point " + std::to_string(i);
            m.fields = {{"Tag", hex(r.tag)}, {"Position", point_text(r.position)},
                        {"Heading", number(r.heading_degrees) + " deg"}};
            record_field(m, r.offset, "POS");
            markers.push_back(std::move(m));
        }
        break;
    }
    case Kind::Eve: {
        const auto read = stage_config::read_eve(bytes);
        if (!read.recognized) break;
        for (std::size_t i = 0; i < read.document.records.size(); ++i) {
            const auto& r = read.document.records[i];
            auto m = make(kind, i, Shape::Area, r.offset);
            m.title = "Event zone " + std::to_string(i);
            m.fields = {{"Kind", std::to_string(r.kind)}, {"Field", std::to_string(r.field0)},
                        {"Argument", std::to_string(r.argument)}};
            area_fields(m, r.area, r.extent);
            record_field(m, r.offset, "EVE");
            markers.push_back(std::move(m));
        }
        break;
    }
    case Kind::Cam: {
        const auto read = stage_config::read_cam(bytes);
        if (!read.recognized) break;
        const auto& doc = read.document;
        for (std::size_t i = 0; i < doc.areas.size(); ++i) {
            const auto& a = doc.areas[i];
            auto m = make(kind, i, Shape::Area, a.offset);
            m.title = "Camera zone " + std::to_string(i);
            m.fields.emplace_back("Source", a.record >= 0
                ? "camera record " + std::to_string(a.record) + " (flags 0x400)"
                : "found by its shape (record layout open)");
            area_fields(m, a.quad, a.extent);
            record_field(m, a.offset, "CAM");
            markers.push_back(std::move(m));
        }
        for (std::size_t i = 0; i < doc.paths.size(); ++i) {
            const auto& p = doc.paths[i];
            auto m = make(kind, i, Shape::Path, p.offset);
            m.label = "CAM path " + std::to_string(i);
            m.title = "Camera path " + std::to_string(i);
            m.points = p.points;
            m.fields = {{"Points", std::to_string(p.points.size())}, {"First", point_text(p.points.front())},
                        {"Last", point_text(p.points.back())}};
            record_field(m, p.offset, "CAM");
            markers.push_back(std::move(m));
        }
        break;
    }
    case Kind::Itm: {
        const auto read = stage_config::read_itm(bytes);
        if (!read.recognized) break;
        for (std::size_t i = 0; i < read.document.records.size(); ++i) {
            const auto& r = read.document.records[i];
            auto m = make(kind, i, Shape::Point, r.offset);
            m.points = {r.position};
            // The unit of the rotation is not settled; no arrow is drawn for it.
            m.title = "Item " + std::to_string(i);
            m.fields = {{"Item id", std::to_string(r.item_id)}, {"Position", point_text(r.position)},
                        {"Rotation about Y", number(r.rotation_y) + " (unit open)"}};
            record_field(m, r.offset, "ITM");
            markers.push_back(std::move(m));
        }
        break;
    }
    case Kind::Ste: {
        const auto read = stage_config::read_ste(bytes);
        if (!read.recognized) break;
        for (std::size_t i = 0; i < read.document.records.size(); ++i) {
            const auto& r = read.document.records[i];
            auto m = make(kind, i, Shape::Point, r.offset);
            m.points = {r.position};
            m.heading_degrees = r.rotation_degrees.y;
            m.title = "Stage element " + std::to_string(i);
            m.fields = {{"Kind", std::to_string(r.kind)}, {"Number", std::to_string(r.number)},
                        {"Position", point_text(r.position)},
                        {"Rotation", point_text(r.rotation_degrees) + " deg"}, {"Scale", point_text(r.scale)}};
            record_field(m, r.offset, "STE");
            markers.push_back(std::move(m));
        }
        break;
    }
    case Kind::Est:
    case Kind::Sef: break;
    }
    return markers;
}

ImagePreview render_plan(std::span<const Marker> markers, std::string_view title, int width, int height) {
    using raster::Canvas;
    const auto rgb = [](Rgb c) { return raster::Rgb{c.r, c.g, c.b}; };
    Canvas canvas(width, height);
    canvas.fill(0, 0, width, height, raster::kBackground);
    const int scale = std::max(2, width / 400);
    const int header = 7 * scale + 7 * scale + 24;
    canvas.text(16, 12, title, raster::kLabel, scale);

    // Legend: one line per kind present.
    {
        int x = 16;
        const int y = 12 + 7 * scale + 8;
        std::vector<std::pair<Kind, std::size_t>> counts;
        for (const auto& m : markers) {
            auto it = std::find_if(counts.begin(), counts.end(), [&](const auto& e) { return e.first == m.source; });
            if (it == counts.end()) counts.emplace_back(m.source, 1U);
            else ++it->second;
        }
        for (const auto& [kind, count] : counts) {
            const auto text = std::to_string(count) + " " + kind_meaning(kind);
            canvas.fill(x, y, x + 5 * scale, y + 5 * scale, rgb(kind_color(kind)));
            x = canvas.text(x + 7 * scale, y, text, raster::kDim, std::max(1, scale - 1)) + 6 * scale;
        }
    }
    if (markers.empty()) {
        canvas.text(16, header + 16, "Nothing placed in this file.", raster::kDim, scale);
        return canvas.take();
    }

    // Bounds over X and Z.
    float min_x = std::numeric_limits<float>::max(), max_x = std::numeric_limits<float>::lowest();
    float min_z = min_x, max_z = max_x;
    for (const auto& m : markers) {
        for (const auto& p : m.points) {
            if (!std::isfinite(p.x) || !std::isfinite(p.z)) continue;
            min_x = std::min(min_x, p.x); max_x = std::max(max_x, p.x);
            min_z = std::min(min_z, p.z); max_z = std::max(max_z, p.z);
        }
    }
    if (min_x > max_x) return canvas.take();
    const float span = std::max({max_x - min_x, max_z - min_z, 1.0F});
    const int left = 16, top = header + 8;
    const int side = std::min(width - 32, height - top - 16);
    const float world_to_px = static_cast<float>(side) / (span * 1.1F);
    const float cx = (min_x + max_x) * 0.5F, cz = (min_z + max_z) * 0.5F;
    const float ox = static_cast<float>(left) + static_cast<float>(width - 32) * 0.5F;
    const float oy = static_cast<float>(top) + static_cast<float>(side) * 0.5F;
    const auto to_px = [&](const Vec3& p) {
        return std::pair<int, int>{static_cast<int>(std::lround(ox + (p.x - cx) * world_to_px)),
                                   static_cast<int>(std::lround(oy + (p.z - cz) * world_to_px))};
    };

    // Grid: a 1-2-5 step giving about eight lines across.
    float step = std::pow(10.0F, std::floor(std::log10(span / 8.0F)));
    for (const float mult : {2.0F, 5.0F, 10.0F}) {
        if (span / step <= 10.0F) break;
        step = std::pow(10.0F, std::floor(std::log10(span / 8.0F))) * mult;
    }
    const float half_view = static_cast<float>(side) * 0.5F / world_to_px;
    for (float gx = std::ceil((cx - half_view) / step) * step; gx <= cx + half_view; gx += step) {
        const auto [px, py] = to_px({gx, 0.0F, cz});
        static_cast<void>(py);
        canvas.line(px, top, px, top + side, raster::kGrid);
    }
    for (float gz = std::ceil((cz - half_view) / step) * step; gz <= cz + half_view; gz += step) {
        const auto [px, py] = to_px({cx, 0.0F, gz});
        static_cast<void>(px);
        canvas.line(left, py, width - 16, py, raster::kGrid);
    }
    canvas.text(left + 4, top + side - 8 * std::max(1, scale - 1),
                "grid " + number(step) + " units  X right, Z down (top view)", raster::kDim, std::max(1, scale - 1));

    const int label_scale = scale;
    for (const auto& m : markers) {
        const auto color = rgb(m.color);
        switch (m.shape) {
        case Shape::Area: {
            for (std::size_t i = 0; i < m.points.size(); ++i) {
                const auto [ax, ay] = to_px(m.points[i]);
                const auto [bx, by] = to_px(m.points[(i + 1U) % m.points.size()]);
                canvas.line(ax, ay, bx, by, color);
            }
            Vec3 centre{};
            for (const auto& p : m.points) { centre.x += p.x / 4.0F; centre.z += p.z / 4.0F; }
            const auto [tx, ty] = to_px(centre);
            canvas.text(tx - Canvas::text_width(std::to_string(m.record), label_scale) / 2, ty - 3 * label_scale,
                        std::to_string(m.record), color, label_scale);
            break;
        }
        case Shape::Path: {
            for (std::size_t i = 0; i + 1U < m.points.size(); ++i) {
                const auto [ax, ay] = to_px(m.points[i]);
                const auto [bx, by] = to_px(m.points[i + 1U]);
                canvas.line(ax, ay, bx, by, color);
            }
            for (const auto& p : m.points) {
                const auto [px, py] = to_px(p);
                canvas.circle(px, py, 2, color);
            }
            break;
        }
        case Shape::Point: {
            const auto [px, py] = to_px(m.points.front());
            for (int radius = 1; radius <= 4 * label_scale; ++radius) canvas.circle(px, py, radius, color);
            if (std::isfinite(m.heading_degrees)) {
                // Heading about Y: 0 looks down +Z.
                const float radians = m.heading_degrees * 3.14159265F / 180.0F;
                const int length = 12 * label_scale;
                canvas.line(px, py, px + static_cast<int>(std::lround(std::sin(radians) * length)),
                            py + static_cast<int>(std::lround(std::cos(radians) * length)), color);
            }
            canvas.text(px + 6 * label_scale, py - 3 * label_scale, std::to_string(m.record), color, label_scale);
            break;
        }
        }
    }
    return canvas.take();
}

}  // namespace dmcresource::stage_markers
