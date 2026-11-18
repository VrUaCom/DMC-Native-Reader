#include "dmc_reader.h"

#include <algorithm>
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
#include "dmcresource/motion/part_attachment.h"
#include "dmcresource/pac_assembly.h"
#include "dmcresource/resource_session.h"
#include "dmcresource/session_lock.h"
#include "dmcresource/composite_placement.h"
#include "dmcresource/spider/model_placement_actions.h"
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

// The core's session lock: the JNI shell takes the same one.
std::recursive_mutex& api_mutex() noexcept { return dmcresource::session_mutex(); }

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

dmcresource::Session* core(dmc_session* session) noexcept {
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

struct dmc_strings {
    std::vector<std::string> items;
};

struct dmc_parts {
    struct Part {
        std::string name;
        std::string state;
        std::vector<std::string> nodes;
        std::vector<const char*> node_names;
    };
    std::vector<Part> parts;
    std::vector<dmc_part> c_parts;
};

struct dmc_motion_strip {
    std::vector<actions::MotionCard> cards;
    std::vector<std::vector<dmc_script_button>> buttons;
    std::vector<dmc_motion_card> c_cards;
};

namespace {

// Sources and their names for assembling or composing; false on a missing one.
bool gather(const dmc_session* const* sessions, const char* const* names, size_t count,
            std::vector<const dmcresource::Session*>* out, std::vector<std::string>* out_names) {
    if (sessions == nullptr || names == nullptr) return false;
    for (size_t index = 0; index < count; ++index) {
        const auto* source = core(sessions[index]);
        if (source == nullptr) return false;
        out->push_back(source);
        out_names->push_back(text_or_empty(names[index]));
    }
    return true;
}

}  // namespace

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

/* ---- Group 1: opening sources, assembling, composing, parts ------------- */

dmc_session* dmc_open_source(const char* name, const uint8_t* bytes, size_t size) {
    try {
        const std::lock_guard lock{api_mutex()};
        if (bytes == nullptr && size != 0U) return nullptr;
        return wrap(dmcresource::open_session(text_or_empty(name), bytes, size));
    } catch (...) { return nullptr; }
}

size_t dmc_strings_count(const dmc_strings* list) { return list == nullptr ? 0U : list->items.size(); }

const char* dmc_strings_at(const dmc_strings* list, size_t index) {
    return list == nullptr || index >= list->items.size() ? nullptr : list->items[index].c_str();
}

void dmc_strings_free(dmc_strings* list) { delete list; }


dmc_session* dmc_assemble(const dmc_session* const* archives, const char* const* names, size_t count,
                          size_t enemy_variant) {
    try {
        const std::lock_guard lock{api_mutex()};
        std::vector<const dmcresource::Session*> sources;
        std::vector<std::string> owned;
        if (count < 1U || !gather(archives, names, count, &sources, &owned)) return nullptr;
        const std::vector<std::string_view> views(owned.begin(), owned.end());
        return wrap(dmcresource::pac_assembly::assemble_archives(sources, views, nullptr, enemy_variant));
    } catch (...) { return nullptr; }
}

dmc_strings* dmc_variant_names(const char* archive_name) {
    try {
        auto out = std::make_unique<dmc_strings>();
        for (const auto& variant : dmcresource::motion::archive_variants(text_or_empty(archive_name))) {
            out->items.push_back(variant.label);
        }
        return out.release();
    } catch (...) { return nullptr; }
}

dmc_session* dmc_compose(const dmc_session* const* parts, const char* const* names, size_t count) {
    try {
        const std::lock_guard lock{api_mutex()};
        std::vector<const dmcresource::Session*> sources;
        std::vector<std::string> owned;
        if (count < 2U || !gather(parts, names, count, &sources, &owned)) return nullptr;
        return wrap(actions::compose_mod_sessions(sources, owned));
    } catch (...) { return nullptr; }
}

bool dmc_attach_ptx(dmc_session* session, int part, const char* name, const uint8_t* bytes, size_t size) {
    try {
        const std::lock_guard lock{api_mutex()};
        auto* target = core(session);
        if (target == nullptr || (bytes == nullptr && size != 0U)) return false;
        const auto file = text_or_empty(name);
        return part < 0 ? actions::attach_ptx(target, file, bytes, size)
                        : actions::attach_ptx_to_part(target, part, file, bytes, size);
    } catch (...) { return false; }
}

dmc_parts* dmc_parts_list(const dmc_session* session) {
    try {
        const std::lock_guard lock{api_mutex()};
        const auto* source = core(session);
        auto out = std::make_unique<dmc_parts>();
        const auto count = std::min<std::size_t>(dmcresource::session_composite_part_count(source), INT32_MAX);
        out->parts.resize(count);
        for (std::size_t index = 0; index < count; ++index) {
            auto& part = out->parts[index];
            const int at = static_cast<int>(index);
            part.name = dmcresource::session_composite_part_name(source, at);
            part.state = dmcresource::describe_composite_part_state(source, at);
            const auto nodes = std::min<std::size_t>(dmcresource::session_composite_part_node_count(source, at),
                                                     INT32_MAX);
            for (std::size_t node = 0; node < nodes; ++node) {
                part.nodes.push_back(
                    dmcresource::session_composite_part_node_name(source, at, static_cast<int>(node)));
            }
        }
        // Pointers last: the strings no longer move.
        for (std::size_t index = 0; index < count; ++index) {
            auto& part = out->parts[index];
            for (const auto& node : part.nodes) part.node_names.push_back(node.c_str());
            const auto selector = dmcresource::session_composite_part_default_attachment_selector(
                source, static_cast<int>(index));
            dmc_part c{};
            c.name = part.name.c_str();
            c.state = part.state.c_str();
            c.node_count = part.node_names.size();
            c.node_names = part.node_names.data();
            c.default_attachment = selector && *selector <= static_cast<std::uint32_t>(INT32_MAX)
                                       ? static_cast<int>(*selector)
                                       : -1;
            out->c_parts.push_back(c);
        }
        return out.release();
    } catch (...) { return nullptr; }
}

size_t dmc_parts_count(const dmc_parts* parts) { return parts == nullptr ? 0U : parts->c_parts.size(); }

const dmc_part* dmc_parts_at(const dmc_parts* parts, size_t index) {
    return parts == nullptr || index >= parts->c_parts.size() ? nullptr : &parts->c_parts[index];
}

void dmc_parts_free(dmc_parts* parts) { delete parts; }

char* dmc_part_attach(dmc_session* session, size_t host, size_t child, uint32_t joint) {
    try {
        const std::lock_guard lock{api_mutex()};
        const auto result = actions::attach_mod_part_to_host_joint(core(session), host, child, joint);
        return copy_string(dmcresource::composite_placement::to_string(result.status));
    } catch (...) { return nullptr; }
}

char* dmc_part_reset(dmc_session* session, size_t child) {
    try {
        const std::lock_guard lock{api_mutex()};
        const auto result = actions::reset_mod_part_placement(core(session), child);
        return copy_string(dmcresource::composite_placement::to_string(result.status));
    } catch (...) { return nullptr; }
}

bool dmc_child_preview_size(const dmc_session* session, size_t index, int* width, int* height) {
    if (width != nullptr) *width = 0;
    if (height != nullptr) *height = 0;
    try {
        const std::lock_guard lock{api_mutex()};
        if (core(session) == nullptr || index > static_cast<size_t>(INT32_MAX)) return false;
        const auto [w, h] = dmcresource::session_child_preview_size(core(session), static_cast<int>(index));
        if (w == 0U || h == 0U || w > INT32_MAX || h > INT32_MAX) return false;
        if (width != nullptr) *width = static_cast<int>(w);
        if (height != nullptr) *height = static_cast<int>(h);
        return true;
    } catch (...) { return false; }
}

bool dmc_child_preview(const dmc_session* session, size_t index, uint8_t* rgba, size_t capacity) {
    try {
        const std::lock_guard lock{api_mutex()};
        if (core(session) == nullptr || rgba == nullptr || index > static_cast<size_t>(INT32_MAX)) return false;
        dmcresource::ImagePreview scratch;
        const auto* image = dmcresource::session_child_preview(core(session), static_cast<int>(index), &scratch);
        if (image == nullptr || !image->available() || image->rgba8.size() > capacity) return false;
        std::memcpy(rgba, image->rgba8.data(), image->rgba8.size());
        return true;
    } catch (...) { return false; }
}

dmc_session* dmc_open_uv_gallery(const dmc_session* session) {
    try {
        const std::lock_guard lock{api_mutex()};
        if (core(session) == nullptr) return nullptr;
        return wrap(dmcresource::open_uv_gallery(core(session)));
    } catch (...) { return nullptr; }
}

bool dmc_has_dual_preview(const dmc_session* session) {
    const std::lock_guard lock{api_mutex()};
    return dmcresource::session_has_dual_preview(core(session));
}

bool dmc_info_preview_active(const dmc_session* session) {
    const std::lock_guard lock{api_mutex()};
    return dmcresource::session_info_preview_active(core(session));
}

bool dmc_set_info_preview(dmc_session* session, bool info) {
    const std::lock_guard lock{api_mutex()};
    return dmcresource::session_set_info_preview(core(session), info);
}

bool dmc_source(const dmc_session* session, const char** name, const uint8_t** bytes, size_t* size) {
    if (name != nullptr) *name = nullptr;
    if (bytes != nullptr) *bytes = nullptr;
    if (size != nullptr) *size = 0U;
    const std::lock_guard lock{api_mutex()};
    const auto* source = core(session);
    if (source == nullptr || !source->authored || source->source_bytes == nullptr) return false;
    if (name != nullptr) *name = source->source_name.c_str();
    if (bytes != nullptr) *bytes = source->source_bytes->data();
    if (size != nullptr) *size = source->source_bytes->size();
    return true;
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
        if (session == nullptr || !session->session || view == nullptr || rgba == nullptr ||
            width < DMC_RENDER_MIN_SIDE || height < DMC_RENDER_MIN_SIDE || width > DMC_RENDER_MAX_SIDE ||
            height > DMC_RENDER_MAX_SIDE) {
            return 0;  // the core draws only these sizes (render_session)
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
