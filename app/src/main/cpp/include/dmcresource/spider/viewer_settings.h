#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// The Settings window, written by the core: every row (section, label,
// choices, the value each choice stores, the default, the hint under it) and
// what the stored values mean (GPU options, render flags, the view's frame
// size and pace). A shell draws the rows one by one, keeps each value under
// its key with the row's storage type, and hands the values back to
// apply_settings. Pieces only the platform can draw (the benchmark, the
// collision kinds) are Slot rows the shell fills in.
namespace dmcresource::spider::settings {

enum class Control : std::uint8_t { Choice = 0, Slider = 1, Slot = 2 };

// How the shell keeps the value; existing Android preferences keep their type.
enum class Storage : std::uint8_t { Bool = 0, Int = 1, Float = 2 };

struct Setting final {
    std::string key;  // the stored key, e.g. "set.gpu"; a Slot's name, e.g. "slot.benchmark"
    std::string section;
    std::string label;
    Control control = Control::Choice;
    Storage storage = Storage::Int;
    std::vector<std::string> choices;  // one label per value (a Slider: one per step)
    std::vector<float> values;         // the stored value of each choice (Bool: 1 or 0)
    float fallback = 0.0F;             // the stored value when nothing is stored yet
    std::string hint;                  // shown under the row; may be empty
    bool render_now = false;           // redraw at once after a change
    bool room_only = false;            // shown only while a base room is loaded
};

// What the GPU of this device offers, for the hints.
struct Capabilities final {
    int max_samples = 0;
    int max_anisotropy = 0;
    std::string renderer;  // what the last frames used
};

[[nodiscard]] Capabilities current_capabilities();
[[nodiscard]] std::vector<Setting> viewer_settings(const Capabilities& capabilities);

// The choice a stored value selects: the matching value, else the default's.
[[nodiscard]] std::size_t selected_choice(const Setting& setting, float stored) noexcept;

// One stored value per key; keys not given take their defaults.
struct Stored final {
    std::vector<std::string> keys;
    std::vector<float> values;
};

// What the stored settings mean for the view.
struct Applied final {
    bool gpu = true;
    bool gpu_present = true;
    bool dmc3_shaders = true;
    bool game_lights = false;
    int msaa = 4;
    bool mipmaps = true;
    int anisotropy = 8;
    std::uint32_t render_flags = 0;  // smooth, unlit, background, room lines, room animation
    int max_side = 720;
    int frame_ms = 33;
    float speed = 1.0F;
    bool shadows = true;
    bool fast_preview = true;
    int mesh_line = 1;
    int collision_line = 1;
};

[[nodiscard]] Applied resolve(const Stored& stored);

// Resolves and sets the core's GPU state (renderer, presentation, shaders,
// lights, MSAA, mipmaps, anisotropy). The rest is for the shell's view.
Applied apply_settings(const Stored& stored);

// "MSAA 4x, mipmaps on, ... , pixel textures": what a benchmark ran with.
[[nodiscard]] std::string benchmark_summary(const Applied& applied);

}  // namespace dmcresource::spider::settings
