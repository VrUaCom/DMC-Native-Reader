#include "dmcresource/spider/stage_actions.h"

#include <array>
#include <utility>

#include "dmcresource/motion/motion_player.h"
#include "dmcresource/spider/cpp23_language.h"
#include "dmcresource/stage_markers.h"

namespace dmcresource::spider::actions {
namespace {

namespace crusader = dmcresource::spider::crusader;
namespace spider_cpp = dmcresource::spider::cpp23;

// ---- load: read members -> place markers -> publish ----------------------

constexpr crusader::OperationId kReadMembers = 1U;
constexpr crusader::OperationId kPublish = 2U;

struct LoadState final {
    std::string_view name;
    std::span<const std::uint8_t> bytes;
    std::shared_ptr<const stage_markers::StageConfig> config;
};

bool read_members(LoadState& s, std::uint32_t) noexcept {
    try {
        // Members found in the archive (or the one member) and every marker
        // they place, in stage space.
        s.config = stage_markers::load_stage_config(s.name, s.bytes);
        return s.config != nullptr;
    } catch (...) {
        return false;
    }
}

bool publish(LoadState& s, std::uint32_t) noexcept {
    if (s.config == nullptr) return false;
    stage_markers::set_current(s.config);
    stage_markers::set_visible(true);
    return true;
}

const crusader::Builder& load_plan() {
    static const crusader::Builder plan = [] {
        crusader::Builder b;
        const auto read = b.add(kReadMembers, 0U, crusader::Domain::cpu, {}, "read members");
        b.add(kPublish, 0U, crusader::Domain::cpu, {read}, "publish");
        return b;
    }();
    return plan;
}

constexpr std::array k_load_bindings{
    spider_cpp::bind<LoadState, &read_members>(kReadMembers),
    spider_cpp::bind<LoadState, &publish>(kPublish),
};

// Every outcome through one constructor, so no field is left to chance.
[[nodiscard]] ActionOutcome outcome(bool ok, ShellStep step = ShellStep::None, std::string message = {},
                                    int marker = -1, std::string title = {}) {
    ActionOutcome out;
    out.ok = ok;
    out.step = step;
    out.message = std::move(message);
    out.marker = marker;
    out.title = std::move(title);
    return out;
}

[[nodiscard]] SoundPcm no_samples() {
    SoundPcm out;
    out.message = "This sound holds no samples";
    return out;
}

// Menu and card ids the shell sends back.
constexpr int kMenuLoad = 1;
constexpr int kMenuToggle = 2;
constexpr int kMenuRemove = 3;
constexpr int kCardOpenFile = 1;
constexpr int kCardClose = 2;

[[nodiscard]] ActionOutcome toggle_markers() {
    const bool show = !stage_markers::visible();
    stage_markers::set_visible(show);
    return outcome(true, ShellStep::None,
                   show ? "Stage config shown: " + stage_config_summary() : std::string{"Stage config hidden"});
}

}  // namespace

ActionOutcome load_stage_config(std::string_view name, const std::uint8_t* bytes, std::size_t size) noexcept {
    try {
        LoadState state{.name = name,
                        .bytes = std::span<const std::uint8_t>{bytes, bytes == nullptr ? 0U : size},
                        .config = nullptr};
        const auto report = spider_cpp::execute(load_plan().build(), k_load_bindings, state);
        if (!report.ok()) {
            return outcome(false, ShellStep::None,
                           "No stage configuration in " + std::string{name} +
                               " (a stNNNcfg.pac or a POS / EVE / CAM / ITM / STE / EST / SEF file)");
        }
        return outcome(true, ShellStep::None, "Stage config: " + stage_config_summary() + ". Hold a marker to see it.");
    } catch (...) {
        return {};
    }
}

ActionOutcome stage_config_tool() noexcept {
    try {
        if (stage_markers::current() == nullptr) return outcome(true, ShellStep::PickStageConfig);
        return toggle_markers();
    } catch (...) {
        return {};
    }
}

Menu stage_config_menu() {
    Menu menu;
    menu.title = "Stage config";
    if (stage_markers::current() == nullptr) {
        menu.message = "Spawn points, event and camera zones, items and stage elements of a stNNNcfg.pac, "
                       "drawn over the stage.";
        menu.items = {{kMenuLoad, "Load a stage config"}};
        return menu;
    }
    menu.message = stage_config_summary();
    menu.items = {{kMenuLoad, "Load another"},
                  {kMenuToggle, stage_markers::visible() ? "Hide" : "Show"},
                  {kMenuRemove, "Remove"}};
    return menu;
}

ActionOutcome run_stage_config_menu(int id) noexcept {
    try {
        switch (id) {
        case kMenuLoad: return outcome(true, ShellStep::PickStageConfig);
        case kMenuToggle:
            if (stage_markers::current() == nullptr) return {};
            return toggle_markers();
        case kMenuRemove:
            stage_markers::set_current(nullptr);
            return outcome(true, ShellStep::None, "Stage config removed");
        default: return {};
        }
    } catch (...) {
        return {};
    }
}

ActionOutcome hold_view(const Session* session, int width, int height, float yaw, float pitch, float zoom,
                        std::uint32_t render_flags, const ViewControls& controls, float x, float y, bool joints,
                        bool scrub) noexcept {
    try {
        if (session == nullptr) return {};
        const auto pick = pick_session(session, width, height, yaw, pitch, zoom, render_flags, controls, x, y);
        if (pick.marker >= 0) {
            stage_markers::set_selected(pick.marker);
            return outcome(true, ShellStep::ShowMarkerCard, {}, pick.marker);
        }
        auto out = outcome(true);
        if (joints && !pick.joint_name.empty()) out.message = "\xF0\x9F\xA6\xB4 " + pick.joint_name;
        if (scrub && motion::has_motion(session)) {
            out.step = ShellStep::StartScrub;
            out.message = out.message.empty() ? std::string{"Scrub: drag sideways"} : out.message + " \xC2\xB7 drag to scrub";
        }
        return out;
    } catch (...) {
        return {};
    }
}

MarkerCard marker_card(int marker) {
    MarkerCard card;
    const auto config = stage_markers::current();
    if (config == nullptr || marker < 0 || static_cast<std::size_t>(marker) >= config->markers.size()) return card;
    const auto& m = config->markers[static_cast<std::size_t>(marker)];
    card.title = m.title;
    card.rows = m.fields;
    if (m.member < config->members.size()) card.rows.emplace_back("File", config->members[m.member].name);
    card.actions = {{kCardOpenFile, "Open file"}, {kCardClose, "Close"}};
    return card;
}

ActionOutcome run_marker_card_action(int marker, int id, std::unique_ptr<Session>* opened) noexcept {
    try {
        if (id == kCardOpenFile) {
            const auto card = marker_card(marker);
            auto file = open_stage_marker_file(marker);
            stage_markers::set_selected(-1);
            if (file == nullptr || opened == nullptr) return outcome(false, ShellStep::None, "Could not open the file");
            *opened = std::move(file);
            return outcome(true, ShellStep::OpenSession, {}, -1, card.title + " \xC2\xB7 file");
        }
        // Close, or the card dismissed.
        stage_markers::set_selected(-1);
        return outcome(true);
    } catch (...) {
        return {};
    }
}

Menu sound_menu(const Session* session) {
    Menu menu;
    menu.title = "Play which sound";
    const auto count = session_sound_count(session);
    for (std::size_t i = 0; i < count; ++i) {
        menu.items.push_back({static_cast<int>(i), session_sound_title(session, i)});
    }
    return menu;
}

SoundPcm sound_pcm(const Session* session, int index) noexcept {
    try {
        if (index < 0) return no_samples();
        SoundPcm out;
        const auto i = static_cast<std::size_t>(index);
        out.samples = session_sound_samples(session, i);
        out.sample_rate = session_sound_rate(session, i);
        if (out.samples.empty() || out.sample_rate == 0U) return no_samples();
        out.message = "\xE2\x96\xB6 " + session_sound_title(session, i);
        return out;
    } catch (...) {
        return no_samples();
    }
}

std::pair<int, int> export_image_size(const Session* session) noexcept {
    try {
        const auto uv = uv_map_export_size(session);
        if (uv.first > 0 && uv.second > 0) return uv;
        const auto* preview = session_active_preview(session);
        if (preview == nullptr) return {0, 0};
        return {static_cast<int>(preview->width), static_cast<int>(preview->height)};
    } catch (...) {
        return {0, 0};
    }
}

RgbaImage render_export_image(const Session* session) noexcept {
    try {
        if (uv_map_export_size(session).first > 0) return render_uv_map_export(session);
        const auto* preview = session_active_preview(session);
        if (preview == nullptr) return {};
        RgbaImage image;
        image.width = static_cast<int>(preview->width);
        image.height = static_cast<int>(preview->height);
        image.pixels = preview->rgba8;
        return image;
    } catch (...) {
        return {};
    }
}

}  // namespace dmcresource::spider::actions
