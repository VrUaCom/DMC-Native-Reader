#include "dmcresource/spider/viewer_settings.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include "dmcresource/view_gpu.h"
#include "dmcresource/view_renderer.h"

namespace dmcresource::spider::settings {
namespace {

constexpr std::string_view kGraphics = "Graphics";
constexpr std::string_view kLines = "Graphics: lines and overlays";
constexpr std::string_view kAnimation = "Animation";
constexpr std::string_view kBaseRoom = "Base room";

// A yes/no row whose first choice stores `first`.
Setting toggle(std::string_view key, std::string_view section, std::string_view label,
               std::vector<std::string> choices, bool first, bool fallback) {
    Setting row;
    row.key = key;
    row.section = section;
    row.label = label;
    row.storage = Storage::Bool;
    row.choices = std::move(choices);
    row.values = {first ? 1.0F : 0.0F, first ? 0.0F : 1.0F};
    row.fallback = fallback ? 1.0F : 0.0F;
    return row;
}

Setting numbers(std::string_view key, std::string_view section, std::string_view label,
                std::vector<std::string> choices, std::vector<float> values, float fallback,
                Storage storage = Storage::Int) {
    Setting row;
    row.key = key;
    row.section = section;
    row.label = label;
    row.storage = storage;
    row.choices = std::move(choices);
    row.values = std::move(values);
    row.fallback = fallback;
    return row;
}

Setting slot(std::string_view key, std::string_view section) {
    Setting row;
    row.key = key;
    row.section = section;
    row.control = Control::Slot;
    return row;
}

[[nodiscard]] float stored_value(const Stored& stored, std::string_view key, float fallback) noexcept {
    const std::size_t count = std::min(stored.keys.size(), stored.values.size());
    for (std::size_t i = 0; i < count; ++i) {
        if (stored.keys[i] == key && std::isfinite(stored.values[i])) return stored.values[i];
    }
    return fallback;
}

[[nodiscard]] const Setting* find(const std::vector<Setting>& rows, std::string_view key) noexcept {
    for (const auto& row : rows) {
        if (row.key == key) return &row;
    }
    return nullptr;
}

}  // namespace

Capabilities current_capabilities() {
    Capabilities out;
    try {
        if (auto* backend = gpu_view_backend()) {
            const auto caps = backend->capabilities();
            out.max_samples = caps.max_samples;
            out.max_anisotropy = static_cast<int>(caps.max_anisotropy);
        }
        const auto stats = gpu_view_stats();
        out.renderer = view_renderer_description() + "\nFrames: " + std::to_string(stats.gpu_frames) + " GPU (" +
                       std::to_string(stats.presented_frames) + " shown straight on the screen), " +
                       std::to_string(stats.cpu_frames) + " CPU" +
                       (stats.gpu_failures != 0U ? ", " + std::to_string(stats.gpu_failures) + " GPU fallbacks"
                                                 : std::string{}) +
                       "\n" + dmc3_shader_report();
    } catch (...) {
    }
    return out;
}

std::vector<Setting> viewer_settings(const Capabilities& caps) {
    std::vector<Setting> rows;

    auto renderer = toggle("set.gpu", kGraphics, "Renderer", {"GPU (graphics chip)", "CPU (software)"}, true, true);
    renderer.hint =
        "GPU draws the model, the room, the floor, shadows and effects with OpenGL ES 3; lines and bones are "
        "drawn on top. Wireframe and UV views always use the CPU. Now: " +
        caps.renderer;
    if (caps.max_samples > 0) {
        renderer.hint += "\nThis chip: anti-aliasing up to " + std::to_string(caps.max_samples) +
                         "x, anisotropic filtering " +
                         (caps.max_anisotropy > 1 ? "up to " + std::to_string(caps.max_anisotropy) + "x."
                                                  : std::string{"not supported."});
    }
    rows.push_back(std::move(renderer));

    auto present = toggle("set.gpuPresent", kGraphics, "Show frames (GPU)",
                          {"Straight on the screen", "Through a picture"}, true, true);
    present.render_now = true;
    present.hint =
        "Straight on the screen: the GPU puts the frame on the display itself, without reading it back to the "
        "CPU and handing it over again. Frames with lines, markers or bones drawn on them, videos and pictures "
        "still go through a picture. Choose \"Through a picture\" if the view stays black or does not change.";
    rows.push_back(std::move(present));

    auto shaders = toggle("set.dmc3Shaders", kGraphics, "Shaders (GPU)", {"DMC3 (the game's)", "Reader's own"},
                          true, true);
    shaders.render_now = true;
    shaders.hint =
        "DMC3: our own shaders that behave like the game's (67 of them, docs/SHADERS.md) draw what the game has "
        "a shader for: textured models and stages lit by their vertex colours, with the game's fog. The rest, "
        "and every frame with \"Reader's own\", uses the Reader's shaders. Info under Renderer says which ones "
        "the last frame used.";
    rows.push_back(std::move(shaders));

    auto lights = toggle("set.gameLights", kGraphics, "Stage lights (DMC3 shaders, experimental)", {"Off", "On"},
                         false, false);
    lights.render_now = true;
    lights.hint =
        "On: the light table of the stage's configuration (stNNNcfg.pac, LIG2) lights the stage and the models "
        "as the game does: its choice of lights (the stage's point lights, the models' directional ones), its "
        "packing and the table's ambient colours. Still experimental: the lights are chosen for the middle of "
        "the view and the model's centre, not for each object, and the stage's colour units are the best "
        "reading so far. Needs the configuration loaded (⋮ → Stage config (stNNNcfg.pac)).";
    rows.push_back(std::move(lights));

    rows.push_back(numbers("set.gpuMsaa", kGraphics, "Anti-aliasing (MSAA, GPU): smooth edges",
                           {"Off", "2x", "4x", "8x"}, {0, 2, 4, 8}, 4));
    rows.push_back(toggle("set.gpuMipmaps", kGraphics, "Mipmaps (GPU): no shimmer on distant textures",
                          {"On", "Off"}, true, true));
    auto aniso = numbers("set.gpuAnisotropy", kGraphics,
                         "Anisotropic filtering (GPU, smooth textures): sharp floors at an angle",
                         {"Off", "2x", "4x", "8x", "16x"}, {1, 2, 4, 8, 16}, 8);
    aniso.hint =
        "Higher anti-aliasing costs memory at large resolutions: above the chip's limit or about 256 MB of "
        "samples the next lower level is used. The CPU renderer ignores these three.";
    rows.push_back(std::move(aniso));

    auto side = numbers("set.maxSide", kGraphics, "Resolution (longest side, px; 2K-8K render larger than the screen)",
                        {"360", "540", "720", "1024", "2K", "4K", "5K", "6K", "8K"},
                        {360, 540, 720, 1024, 2048, 3840, 5120, 6144, 7680}, 720);
    side.hint =
        "2K = 2048, 4K = 3840, 5K = 5120, 6K = 6144, 8K = 7680 px. They need a lot of memory and time per "
        "frame; while the view moves a small preview is shown. If memory runs out the next lower size is "
        "chosen.";
    rows.push_back(std::move(side));

    rows.push_back(toggle("set.fastPreview", kGraphics, "While moving (drag, flick, animation)",
                          {"Fast preview (half size)", "Full quality"}, true, true));
    rows.push_back(toggle("set.smooth", kGraphics, "Model textures", {"Pixel (original)", "Smooth"}, false, false));
    rows.push_back(toggle("set.unlit", kGraphics, "Model lighting", {"Camera light", "Off (flat)"}, false, false));
    rows.push_back(numbers("set.background", kGraphics, "Background", {"Dark", "Grey", "Light", "Black"},
                           {0, 1, 2, 3}, 0));
    rows.push_back(toggle("set.shadows", kGraphics, "Shadows when a file opens", {"On", "Off"}, true, true));
    rows.push_back(slot("slot.benchmark", kGraphics));

    const std::vector<std::string> widths{"1", "2", "3", "4", "6"};
    const std::vector<float> width_values{1, 2, 3, 4, 6};
    rows.push_back(numbers("set.meshLine", kLines, "Mesh line width (wireframe, room meshes, bones; px at 720)",
                           widths, width_values, 1));
    rows.push_back(numbers("set.collisionLine", kLines, "Collision line width (HITS, attack shapes; px at 720)",
                           widths, width_values, 1));
    Setting room_lines;
    room_lines.key = "set.roomLines";
    room_lines.section = kLines;
    room_lines.label = "Room mesh lines behind a model (wireframe mode): opacity";
    room_lines.control = Control::Slider;
    room_lines.storage = Storage::Int;
    for (int tenth = 0; tenth <= 10; ++tenth) {
        room_lines.choices.push_back(std::to_string(tenth * 10) + "%" + (tenth == 0 ? "  (hidden)" : ""));
        room_lines.values.push_back(static_cast<float>(tenth));
    }
    room_lines.fallback = 5;
    rows.push_back(std::move(room_lines));
    rows.push_back(slot("slot.collisionKinds", kLines));

    rows.push_back(numbers("set.frameMs", kAnimation, "Animation frame rate", {"20 fps", "30 fps", "60 fps"},
                           {50, 33, 16}, 33));
    rows.push_back(numbers("set.speed", kAnimation, "Playback speed", {"¼×", "½×", "1×", "2×"},
                           {0.25F, 0.5F, 1.0F, 2.0F}, 1.0F, Storage::Float));

    auto room_anim = toggle("set.roomAnimate", kBaseRoom, "Room animation (sky clouds, stage effects)",
                            {"On", "Off"}, true, true);
    room_anim.room_only = true;
    rows.push_back(std::move(room_anim));
    return rows;
}

std::size_t selected_choice(const Setting& setting, float stored) noexcept {
    std::size_t fallback = 0;
    for (std::size_t i = 0; i < setting.values.size(); ++i) {
        if (std::fabs(setting.values[i] - stored) < 0.01F) return i;
        if (std::fabs(setting.values[i] - setting.fallback) < 0.01F) fallback = i;
    }
    return fallback;
}

Applied resolve(const Stored& stored) {
    const auto rows = viewer_settings({});
    const auto value = [&](std::string_view key) {
        const Setting* row = find(rows, key);
        return stored_value(stored, key, row == nullptr ? 0.0F : row->fallback);
    };
    const auto on = [&](std::string_view key) { return value(key) != 0.0F; };
    const auto whole = [&](std::string_view key) { return static_cast<int>(std::lround(value(key))); };

    Applied out;
    out.gpu = on("set.gpu");
    out.gpu_present = on("set.gpuPresent");
    out.dmc3_shaders = on("set.dmc3Shaders");
    out.game_lights = on("set.gameLights");
    out.msaa = whole("set.gpuMsaa");
    out.mipmaps = on("set.gpuMipmaps");
    out.anisotropy = whole("set.gpuAnisotropy");
    out.max_side = std::clamp(whole("set.maxSide"), 128, 7680);
    out.frame_ms = std::max(8, whole("set.frameMs"));
    const float speed = value("set.speed");
    out.speed = speed > 0.0F ? speed : 1.0F;
    out.shadows = on("set.shadows");
    out.fast_preview = on("set.fastPreview");
    out.mesh_line = std::clamp(whole("set.meshLine"), 1, 16);
    out.collision_line = std::clamp(whole("set.collisionLine"), 1, 16);

    std::uint32_t flags = 0;
    if (on("set.smooth")) flags |= render_flag(RenderFlag::SmoothTextures);
    if (on("set.unlit")) flags |= render_flag(RenderFlag::Unlit);
    flags |= (static_cast<std::uint32_t>(whole("set.background")) & 3U) << kRenderBackgroundShift;
    flags |= static_cast<std::uint32_t>(std::clamp(whole("set.roomLines"), 0, 10) + 1) << kRenderRoomLinesShift;
    if (on("set.roomAnimate")) flags |= render_flag(RenderFlag::RoomAnimate);
    out.render_flags = flags;
    return out;
}

Applied apply_settings(const Stored& stored) {
    const Applied applied = resolve(stored);
    set_gpu_view_enabled(applied.gpu);
    set_gpu_present_enabled(applied.gpu_present);
    set_dmc3_shaders_enabled(applied.dmc3_shaders);
    set_game_lights_enabled(applied.game_lights);
    set_gpu_view_options({applied.msaa, applied.mipmaps, applied.anisotropy});
    return applied;
}

std::string benchmark_summary(const Applied& applied) {
    std::string text;
    if (applied.gpu) {
        text += "MSAA " + (applied.msaa == 0 ? std::string{"off"} : std::to_string(applied.msaa) + "x");
        text += std::string{", mipmaps "} + (applied.mipmaps ? "on" : "off");
        text += ", anisotropy " +
                (applied.anisotropy <= 1 ? std::string{"off"} : std::to_string(applied.anisotropy) + "x");
        text += std::string{", shown "} + (applied.gpu_present ? "directly" : "through a picture");
        text += std::string{", shaders "} + (applied.dmc3_shaders ? "DMC3" : "Reader");
        if (applied.game_lights) text += " with the stage's lights";
        text += ", ";
    }
    text += has_render_flag(applied.render_flags, RenderFlag::SmoothTextures) ? "smooth" : "pixel";
    text += " textures";
    if (has_render_flag(applied.render_flags, RenderFlag::Unlit)) text += ", unlit";
    return text;
}

}  // namespace dmcresource::spider::settings
