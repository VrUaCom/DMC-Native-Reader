#include "dmc_reader.h"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "dmcresource/camera_control.h"
#include "dmcresource/file_health.h"
#include "dmcresource/motion/motion_player.h"
#include "dmcresource/resource_session.h"
#include "dmcresource/spider/app_info.h"
#include "dmcresource/spider/camera.h"
#include "dmcresource/spider/motion_strip.h"
#include "dmcresource/spider/session_actions.h"
#include "dmcresource/spider/viewer_settings.h"

struct dmc_session {
    std::unique_ptr<dmcresource::Session> session;
};

namespace {

namespace actions = dmcresource::spider::actions;
namespace settings = dmcresource::spider::settings;

std::recursive_mutex& api_mutex() {
    static std::recursive_mutex mutex;
    return mutex;
}

char* copy_string(const std::string& text) noexcept {
    auto* out = static_cast<char*>(std::malloc(text.size() + 1U));
    if (out == nullptr) return nullptr;
    std::memcpy(out, text.c_str(), text.size() + 1U);
    return out;
}

std::string text_or_empty(const char* text) { return text == nullptr ? std::string{} : std::string{text}; }

const dmcresource::Session* core(const dmc_session* session) noexcept {
    return session == nullptr ? nullptr : session->session.get();
}

dmc_session* wrap(std::unique_ptr<dmcresource::Session> session) {
    if (!session) return nullptr;
    auto* out = new dmc_session{};
    out->session = std::move(session);
    return out;
}

}  // namespace

struct dmc_settings {
    std::vector<settings::Setting> rows;
    std::vector<std::vector<const char*>> choices;
    std::vector<dmc_setting_row> c_rows;
};

struct dmc_motion_strip {
    std::vector<actions::MotionCard> cards;
    std::vector<std::vector<dmc_script_button>> buttons;
    std::vector<dmc_motion_card> c_cards;
};

extern "C" {

void dmc_string_free(char* text) { std::free(text); }

dmc_open_result dmc_open(const char* name, const uint8_t* bytes, size_t size) {
    dmc_open_result result{};
    try {
        const std::lock_guard lock{api_mutex()};
        auto opened = actions::open_resource(text_or_empty(name), bytes, size);
        result.opened_as = static_cast<dmc_opened_as>(opened.as);
        result.title = copy_string(opened.title);
        result.room_effects = opened.room_effects;
        result.session = wrap(std::move(opened.session));
    } catch (...) {
        result.session = nullptr;
        result.opened_as = DMC_OPENED_REJECTED;
    }
    return result;
}

void dmc_close(dmc_session* session) {
    const std::lock_guard lock{api_mutex()};
    delete session;
}

size_t dmc_child_count(const dmc_session* session) {
    const std::lock_guard lock{api_mutex()};
    return dmcresource::session_child_count(core(session));
}

char* dmc_child_title(const dmc_session* session, size_t index) {
    try {
        const std::lock_guard lock{api_mutex()};
        if (core(session) == nullptr || index > static_cast<size_t>(INT32_MAX)) return nullptr;
        return copy_string(dmcresource::session_child_title(core(session), static_cast<int>(index)));
    } catch (...) { return nullptr; }
}

dmc_session* dmc_open_child(const dmc_session* session, size_t index) {
    try {
        const std::lock_guard lock{api_mutex()};
        if (core(session) == nullptr || index > static_cast<size_t>(INT32_MAX)) return nullptr;
        return wrap(dmcresource::open_session_child(core(session), static_cast<int>(index)));
    } catch (...) { return nullptr; }
}

uint64_t dmc_state_flags(const dmc_session* session) {
    try {
        const std::lock_guard lock{api_mutex()};
        return static_cast<uint64_t>(dmcresource::black_widow_state(core(session)));
    } catch (...) { return 0; }
}

char* dmc_welcome_text(const char* app_version, const char* native_build) {
    try {
        return copy_string(actions::welcome_text(text_or_empty(app_version), text_or_empty(native_build)));
    } catch (...) { return nullptr; }
}

char* dmc_info_text(const dmc_session* session, const char* title, size_t navigation_depth, const char* back_title,
                    const dmc_staged_file* staged, size_t staged_count) {
    try {
        const std::lock_guard lock{api_mutex()};
        const std::string title_text = text_or_empty(title);
        const std::string back_text = text_or_empty(back_title);
        actions::InfoContext context;
        context.title = title_text;
        context.navigation_depth = navigation_depth;
        context.back_title = back_text;
        for (size_t index = 0; staged != nullptr && index < staged_count; ++index) {
            context.staged.push_back({text_or_empty(staged[index].role), text_or_empty(staged[index].name)});
        }
        return copy_string(actions::session_info_text(core(session), context));
    } catch (...) { return nullptr; }
}

int dmc_file_health(const dmc_session* session, const char* filename, char** report) {
    if (report != nullptr) *report = nullptr;
    try {
        const std::lock_guard lock{api_mutex()};
        if (core(session) == nullptr) return 0;
        const auto health = dmcresource::file_health::session_report(*core(session));
        if (report != nullptr) *report = copy_string(dmcresource::file_health::format(text_or_empty(filename), health));
        return static_cast<int>(health.worst());
    } catch (...) { return 0; }
}

bool dmc_image_preview_size(const dmc_session* session, int* width, int* height) {
    if (width != nullptr) *width = 0;
    if (height != nullptr) *height = 0;
    const std::lock_guard lock{api_mutex()};
    const auto* preview = dmcresource::session_active_preview(core(session));
    if (preview == nullptr || !preview->available() || preview->width > INT32_MAX || preview->height > INT32_MAX) {
        return false;
    }
    if (width != nullptr) *width = static_cast<int>(preview->width);
    if (height != nullptr) *height = static_cast<int>(preview->height);
    return true;
}

bool dmc_image_preview(const dmc_session* session, uint8_t* rgba, size_t capacity) {
    const std::lock_guard lock{api_mutex()};
    const auto* preview = dmcresource::session_active_preview(core(session));
    if (preview == nullptr || !preview->available() || rgba == nullptr || preview->rgba8.size() > capacity) {
        return false;
    }
    std::memcpy(rgba, preview->rgba8.data(), preview->rgba8.size());
    return true;
}

int dmc_render(dmc_session* session, int width, int height, const dmc_view* view, uint8_t* rgba, size_t capacity) {
    try {
        const std::lock_guard lock{api_mutex()};
        if (session == nullptr || !session->session || view == nullptr || rgba == nullptr || width <= 0 ||
            height <= 0) {
            return 0;
        }
        int status = 1;
        if (std::isfinite(view->motion_frame) &&
            !dmcresource::motion::apply_motion_frame(session->session.get(), view->motion_frame)) {
            status = 2;
        }
        dmcresource::ViewControls controls{view->pan_x, view->pan_y, view->room_yaw, view->follow, view->dolly};
        const auto camera = dmcresource::camera::current();
        controls.fly = camera.fly;
        controls.eye = camera.eye;
        controls.has_pivot = camera.has_pivot;
        controls.pivot = camera.pivot;
        const auto image = dmcresource::spider::camera::render_view_frame(
            session->session.get(), width, height, view->yaw, view->pitch, view->zoom, view->render_flags, controls);
        if (image.width != width || image.height != height || image.pixels.size() > capacity ||
            image.pixels.empty()) {
            return 0;
        }
        std::memcpy(rgba, image.pixels.data(), image.pixels.size());
        return status;
    } catch (...) { return 0; }
}

dmc_settings* dmc_settings_schema(bool describe_device) {
    try {
        auto out = std::make_unique<dmc_settings>();
        out->rows = settings::viewer_settings(describe_device ? settings::current_capabilities()
                                                              : settings::Capabilities{});
        out->choices.resize(out->rows.size());
        for (size_t index = 0; index < out->rows.size(); ++index) {
            const auto& row = out->rows[index];
            for (const auto& choice : row.choices) out->choices[index].push_back(choice.c_str());
            dmc_setting_row c{};
            c.key = row.key.c_str();
            c.section = row.section.c_str();
            c.label = row.label.c_str();
            c.control = static_cast<int>(row.control);
            c.storage = static_cast<int>(row.storage);
            c.choice_count = row.choices.size();
            c.choices = out->choices[index].data();
            c.values = row.values.data();
            c.fallback = row.fallback;
            c.hint = row.hint.c_str();
            c.render_now = row.render_now;
            c.room_only = row.room_only;
            out->c_rows.push_back(c);
        }
        return out.release();
    } catch (...) { return nullptr; }
}

size_t dmc_settings_count(const dmc_settings* list) { return list == nullptr ? 0U : list->c_rows.size(); }

const dmc_setting_row* dmc_settings_row(const dmc_settings* list, size_t index) {
    return list == nullptr || index >= list->c_rows.size() ? nullptr : &list->c_rows[index];
}

void dmc_settings_free(dmc_settings* list) { delete list; }

size_t dmc_settings_selected(const dmc_setting_row* row, float stored) {
    if (row == nullptr || row->values == nullptr) return 0;
    settings::Setting setting;
    setting.values.assign(row->values, row->values + row->choice_count);
    setting.fallback = row->fallback;
    return settings::selected_choice(setting, stored);
}

dmc_applied_settings dmc_apply_settings(const char* const* keys, const float* values, size_t count) {
    dmc_applied_settings out{};
    try {
        settings::Stored stored;
        for (size_t index = 0; keys != nullptr && values != nullptr && index < count; ++index) {
            stored.keys.push_back(text_or_empty(keys[index]));
            stored.values.push_back(values[index]);
        }
        const std::lock_guard lock{api_mutex()};
        const auto applied = settings::apply_settings(stored);
        out.render_flags = applied.render_flags;
        out.max_side = applied.max_side;
        out.frame_ms = applied.frame_ms;
        out.speed = applied.speed;
        out.shadows = applied.shadows;
        out.fast_preview = applied.fast_preview;
        out.mesh_line = applied.mesh_line;
        out.collision_line = applied.collision_line;
    } catch (...) {
    }
    return out;
}

dmc_motion_strip* dmc_motion_strip_build(const dmc_session* session, bool with_library, const char* const* staged,
                                         size_t staged_count) {
    try {
        std::vector<std::string> names;
        for (size_t index = 0; staged != nullptr && index < staged_count; ++index) {
            names.push_back(text_or_empty(staged[index]));
        }
        auto out = std::make_unique<dmc_motion_strip>();
        {
            const std::lock_guard lock{api_mutex()};
            out->cards = actions::motion_strip(core(session), with_library, names);
        }
        out->buttons.resize(out->cards.size());
        for (size_t index = 0; index < out->cards.size(); ++index) {
            const auto& card = out->cards[index];
            for (const auto& button : card.scripts) {
                out->buttons[index].push_back(
                    {button.script_index, button.slot, button.label.c_str(), button.description.c_str()});
            }
            dmc_motion_card c{};
            c.name = card.name.c_str();
            c.library_index = card.library_index;
            c.staged_index = card.staged_index;
            c.pack_slot = card.pack_slot;
            c.separator_before = card.separator_before;
            c.separator_description = card.separator_description.c_str();
            c.kind = card.kind.c_str();
            c.stem = card.stem.c_str();
            c.description = card.description.c_str();
            c.script_count = out->buttons[index].size();
            c.scripts = out->buttons[index].data();
            out->c_cards.push_back(c);
        }
        return out.release();
    } catch (...) { return nullptr; }
}

size_t dmc_motion_strip_count(const dmc_motion_strip* strip) { return strip == nullptr ? 0U : strip->c_cards.size(); }

const dmc_motion_card* dmc_motion_strip_card(const dmc_motion_strip* strip, size_t index) {
    return strip == nullptr || index >= strip->c_cards.size() ? nullptr : &strip->c_cards[index];
}

void dmc_motion_strip_free(dmc_motion_strip* strip) { delete strip; }

}  // extern "C"
