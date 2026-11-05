// The Settings window is the core's: rows, defaults and what the stored
// values mean, the same for every shell.
#include "dmcresource/spider/viewer_settings.h"
#include "dmcresource/view_renderer.h"

#include <cassert>
#include <set>
#include <string>

namespace settings = dmcresource::spider::settings;

int main() {
    settings::Capabilities caps;
    caps.max_samples = 8;
    caps.max_anisotropy = 16;
    caps.renderer = "test renderer";
    const auto rows = settings::viewer_settings(caps);

    // Keys are unique, every choice row has one value per label, its default
    // is one of its values, and the slots name the platform pieces.
    std::set<std::string> keys;
    bool benchmark = false;
    bool kinds = false;
    for (const auto& row : rows) {
        assert(keys.insert(row.key).second);
        assert(!row.section.empty());
        if (row.control == settings::Control::Slot) {
            benchmark = benchmark || row.key == "slot.benchmark";
            kinds = kinds || row.key == "slot.collisionKinds";
            continue;
        }
        assert(!row.label.empty() && row.choices.size() == row.values.size() && !row.values.empty());
        const auto chosen = settings::selected_choice(row, row.fallback);
        assert(row.values[chosen] == row.fallback);
    }
    assert(benchmark && kinds);
    assert(rows.front().key == "set.gpu");
    assert(rows.front().hint.find("Now: test renderer") != std::string::npos);
    assert(rows.front().hint.find("anti-aliasing up to 8x, anisotropic filtering up to 16x.") != std::string::npos);

    // An unknown stored value selects the default; a known one selects itself.
    const settings::Setting* side = nullptr;
    for (const auto& row : rows) if (row.key == "set.maxSide") side = &row;
    assert(side != nullptr && side->choices[settings::selected_choice(*side, 999.0F)] == "720");
    assert(side->choices[settings::selected_choice(*side, 7680.0F)] == "8K");

    // Nothing stored: the defaults the Android app always had.
    const auto defaults = settings::resolve({});
    assert(defaults.gpu && defaults.gpu_present && defaults.dmc3_shaders && !defaults.game_lights);
    assert(defaults.msaa == 4 && defaults.mipmaps && defaults.anisotropy == 8);
    assert(defaults.max_side == 720 && defaults.frame_ms == 33 && defaults.speed == 1.0F);
    assert(defaults.shadows && defaults.fast_preview && defaults.mesh_line == 1 && defaults.collision_line == 1);
    using dmcresource::RenderFlag;
    using dmcresource::render_flag;
    assert(defaults.render_flags == ((5U + 1U) << dmcresource::kRenderRoomLinesShift | render_flag(RenderFlag::RoomAnimate)));

    // Stored values pack into the render flags as the shell used to.
    settings::Stored stored;
    stored.keys = {"set.smooth", "set.unlit", "set.background", "set.roomLines", "set.roomAnimate", "set.speed",
                   "set.gpu", "set.maxSide"};
    stored.values = {1, 1, 2, 0, 0, 0.5F, 0, 99999};
    const auto applied = settings::resolve(stored);
    assert(applied.render_flags == (render_flag(RenderFlag::SmoothTextures) | render_flag(RenderFlag::Unlit) |
                                    (2U << dmcresource::kRenderBackgroundShift) |
                                    (1U << dmcresource::kRenderRoomLinesShift)));
    assert(applied.speed == 0.5F && !applied.gpu && applied.max_side == 7680);
    assert(settings::benchmark_summary(applied) == "smooth textures, unlit");
    assert(settings::benchmark_summary(defaults) ==
           "MSAA 4x, mipmaps on, anisotropy 8x, shown directly, shaders DMC3, pixel textures");
    return 0;
}
