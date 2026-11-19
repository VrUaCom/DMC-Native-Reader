#include "dmc_reader.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_set>
#include <vector>

#include "dmcresource/camera_control.h"
#include "dmcresource/file_health.h"
#include "dmcresource/motion/motion_player.h"
#include "dmcresource/motion/enemy_effects.h"
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
#include "dmcresource/spider/stage_actions.h"
#include "dmcresource/stage_room.h"
#include "dmcresource/frame_profile.h"
#include "dmcresource/view_benchmark.h"
#include "dmcresource/view_gpu.h"
#include "dmcresource/view_renderer.h"
#include "dmcresource/view_gesture.h"
#include "dmcresource/stage_markers.h"
#include "dmcresource/stage_slot.h"
#include "dmcresource/collision_debug.h"
#include "dmcresource/environment_collision.h"
#include "dmcresource/spider/collision_report.h"
#include "dmcresource/spider/companion_menu.h"
#include "dmcresource/demo_session.h"
#include "dmcresource/messages.h"
#include "dmcresource/crash_recorder.h"
#include "dmcresource/session_inspection.h"
#include "dmcresource/inspection_format.h"

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

// B9: in a debug build (no NDEBUG) every live handle is registered, and a
// handle the API never made, or one already closed, stops the process with a
// message instead of being used (undefined behaviour in a release build).
#if !defined(NDEBUG) || defined(DMC_READER_HANDLE_REGISTRY)
std::mutex& registry_mutex() noexcept {
    static std::mutex mutex;
    return mutex;
}
std::unordered_set<const dmc_session*>& registry() {
    static auto* live = new std::unordered_set<const dmc_session*>{};  // outlives static teardown
    return *live;
}
void remember(const dmc_session* session) {
    const std::lock_guard lock{registry_mutex()};
    registry().insert(session);
}
void check_live(const dmc_session* session, const char* what) noexcept {
    bool live = false;
    {
        const std::lock_guard lock{registry_mutex()};
        live = registry().contains(session);
    }
    if (!live) {
        std::fprintf(stderr, "dmc_reader: %s on a handle that is not open (%p): closed twice or never opened\n",
                     what, static_cast<const void*>(session));
        std::abort();
    }
}
void forget(const dmc_session* session) noexcept {
    check_live(session, "dmc_close");
    const std::lock_guard lock{registry_mutex()};
    registry().erase(session);
}
#else
void remember(const dmc_session*) {}
void check_live(const dmc_session*, const char*) noexcept {}
void forget(const dmc_session*) noexcept {}
#endif

const dmcresource::Session* core(const dmc_session* session) noexcept {
    if (session == nullptr) return nullptr;
    check_live(session, "a call");
    return session->session.get();
}

dmcresource::Session* core(dmc_session* session) noexcept {
    if (session == nullptr) return nullptr;
    check_live(session, "a call");
    return session->session.get();
}

dmc_session* wrap(std::unique_ptr<dmcresource::Session> session) {
    if (!session) return nullptr;
    auto out = std::make_unique<dmc_session>();
    out->session = std::move(session);
    remember(out.get());
    return out.release();
}

// The view's controls as the core reads them: the shell's pan, room yaw,
// follow and dolly, plus the camera's own state (fly eye, pivot).
dmcresource::ViewControls controls_of(const dmc_view& view) {
    dmcresource::ViewControls controls{view.pan_x, view.pan_y, view.room_yaw, view.follow, view.dolly};
    const auto camera = dmcresource::camera::current();
    controls.fly = camera.fly;
    controls.eye = camera.eye;
    controls.has_pivot = camera.has_pivot;
    controls.pivot = camera.pivot;
    return controls;
}

bool valid_side(int side) noexcept { return side >= DMC_RENDER_MIN_SIDE && side <= DMC_RENDER_MAX_SIDE; }

dmc_image* image_of(const dmcresource::RgbaImage& image) {
    if (image.width <= 0 || image.height <= 0 || image.pixels.empty()) return nullptr;
    const auto bytes = static_cast<std::size_t>(image.width) * static_cast<std::size_t>(image.height) * 4U;
    if (image.pixels.size() < bytes) return nullptr;
    auto* out = static_cast<dmc_image*>(std::calloc(1U, sizeof(dmc_image)));
    if (out == nullptr) return nullptr;
    out->rgba = static_cast<std::uint8_t*>(std::malloc(bytes));
    if (out->rgba == nullptr) { std::free(out); return nullptr; }
    std::memcpy(out->rgba, image.pixels.data(), bytes);
    out->width = image.width;
    out->height = image.height;
    return out;
}

dmc_outcome outcome_of(const dmcresource::spider::actions::ActionOutcome& o) {
    dmc_outcome out{};
    out.ok = o.ok;
    out.step = static_cast<int>(o.step);
    out.message = copy_string(o.message);
    out.marker = o.marker;
    out.title = copy_string(o.title);
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
        // No session lock (B7): parsing reads only `bytes` and builds a new
        // session no one else sees; the core's shared state it reads (stage
        // rooms, markers, camera) has its own locks. A large PAC no longer
        // stops other sessions' frames while it is read.
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

dmc_open_result dmc_open_description(const char* name, const uint8_t* bytes, size_t size) {
    dmc_open_result result{};
    try {
        // Unlocked like dmc_open (B7): nothing is parsed, a new session is made.
        auto opened = actions::open_resource_description(text_or_empty(name), bytes, size);
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
    if (session == nullptr) return;
    const std::lock_guard lock{api_mutex()};
    forget(session);
    delete session;
}

/* ---- Group 1: opening sources, assembling, composing, parts ------------- */

dmc_session* dmc_open_source(const char* name, const uint8_t* bytes, size_t size) {
    try {
        if (bytes == nullptr && size != 0U) return nullptr;
        // Parsed without the session lock, as dmc_open (B7).
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
        if (core(session) == nullptr || view == nullptr || rgba == nullptr ||
            width < DMC_RENDER_MIN_SIDE || height < DMC_RENDER_MIN_SIDE || width > DMC_RENDER_MAX_SIDE ||
            height > DMC_RENDER_MAX_SIDE) {
            return 0;  // the core draws only these sizes (render_session)
        }
        int status = 1;
        if (std::isfinite(view->motion_frame) &&
            !dmcresource::motion::apply_motion_frame(session->session.get(), view->motion_frame)) {
            status = 2;
        }
        const auto controls = controls_of(*view);
        const auto image = dmcresource::spider::camera::render_view_frame(
            session->session.get(), width, height, view->yaw, view->pitch, view->zoom, view->render_flags, controls);
        if (image.width != width || image.height != height) return 0;
        if (image.pixels.empty()) {
            // Only a frame drawn straight to an attached window comes back without pixels.
            if (!controls.present) return 0;
            dmcresource::frame_profile::frame_done();
            return status | DMC_RENDER_SHOWN;
        }
        if (image.pixels.size() > capacity) return 0;
        std::memcpy(rgba, image.pixels.data(), image.pixels.size());
        dmcresource::frame_profile::frame_done();
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

/* ---- Group 2: picking, holding, camera, fly, export, capture ------------- */

dmc_pick_result dmc_pick(dmc_session* session, int width, int height, const dmc_view* view, float x, float y,
                         bool place) {
    dmc_pick_result out{};
    out.joint = -1;
    out.marker = -1;
    try {
        const std::lock_guard lock{api_mutex()};
        if (core(session) == nullptr || view == nullptr || !valid_side(width) || !valid_side(height)) {
            out.joint_name = copy_string({});
            out.marker_title = copy_string({});
            return out;
        }
        const auto pick = dmcresource::pick_session(core(session), width, height, view->yaw, view->pitch,
                                                    view->zoom, view->render_flags, controls_of(*view), x, y);
        out.kind = pick.model ? DMC_PICK_MODEL : pick.room ? DMC_PICK_ROOM : DMC_PICK_NONE;
        if (pick.room && pick.room_floor && !pick.model && place) {
            dmcresource::stage_room::place_at(pick.room_point);
            out.kind = DMC_PICK_PLACED;
        }
        out.joint = pick.joint;
        out.marker = pick.marker;
        out.room_point[0] = pick.room_point.x;
        out.room_point[1] = pick.room_point.y;
        out.room_point[2] = pick.room_point.z;
        out.joint_name = copy_string(pick.joint_name);
        out.marker_title = copy_string(pick.marker_title);
    } catch (...) {
        if (out.joint_name == nullptr) out.joint_name = copy_string({});
        if (out.marker_title == nullptr) out.marker_title = copy_string({});
    }
    return out;
}

void dmc_pick_result_free(dmc_pick_result* result) {
    if (result == nullptr) return;
    std::free(result->joint_name);
    std::free(result->marker_title);
    result->joint_name = nullptr;
    result->marker_title = nullptr;
}

void dmc_outcome_free(dmc_outcome* outcome) {
    if (outcome == nullptr) return;
    std::free(outcome->message);
    std::free(outcome->title);
    outcome->message = nullptr;
    outcome->title = nullptr;
}

dmc_outcome dmc_hold(const dmc_session* session, int width, int height, const dmc_view* view, float x, float y,
                     bool joints, bool scrub) {
    try {
        const std::lock_guard lock{api_mutex()};
        if (view == nullptr) return outcome_of({});
        return outcome_of(actions::hold_view(core(session), width, height, view->yaw, view->pitch, view->zoom,
                                             view->render_flags, controls_of(*view), x, y, joints, scrub));
    } catch (...) { return outcome_of({}); }
}

bool dmc_camera_metrics(const dmc_session* session, float* distance, float* dolly_limit) {
    try {
        const std::lock_guard lock{api_mutex()};
        if (core(session) == nullptr) return false;
        if (distance != nullptr) *distance = dmcresource::session_camera_distance(core(session));
        if (dolly_limit != nullptr) *dolly_limit = dmcresource::session_dolly_limit(core(session));
        return true;
    } catch (...) { return false; }
}

bool dmc_camera_mode(void) { return dmcresource::spider::camera::finder_on(); }

bool dmc_claim_gesture(int gesture) {
    if (gesture < DMC_GESTURE_NONE || gesture > DMC_GESTURE_TURN) return false;
    return dmcresource::camera::claim_gesture(static_cast<dmcresource::camera::Gesture>(gesture));
}

void dmc_end_touch(void) { dmcresource::camera::end_touch(); }
void dmc_camera_reset(void) { dmcresource::camera::reset(); }

bool dmc_zoom_step(float touch_zoom, float next, bool settling) {
    return dmcresource::camera::zoom_step(touch_zoom, next, settling);
}

void dmc_camera_level(void) { dmcresource::spider::camera::level_camera(); }
float dmc_camera_roll(float radians) { return dmcresource::spider::camera::turn_roll(radians); }
bool dmc_camera_holds_orientation(void) { return dmcresource::spider::camera::holds_screen_orientation(); }

bool dmc_gyro_active(void) { return dmcresource::spider::camera::gyro_active(); }
void dmc_gyro_reset(void) { dmcresource::spider::camera::gyro_reset(); }

void dmc_gyro_turn(float heading, float elevation, float roll, float out[2]) {
    if (out == nullptr) return;
    const auto turn = dmcresource::spider::camera::gyro_turn(heading, elevation, roll);
    out[0] = turn[0];
    out[1] = turn[1];
}

bool dmc_fly_enter(const dmc_session* session, int width, int height, const dmc_view* view) {
    try {
        const std::lock_guard lock{api_mutex()};
        if (view == nullptr) return false;
        const auto eye = dmcresource::session_camera_eye(core(session), width, height, view->yaw, view->pitch,
                                                         view->zoom, view->render_flags, controls_of(*view));
        if (!eye) return false;
        dmcresource::camera::enter_fly(*eye);
        return true;
    } catch (...) { return false; }
}

dmc_fly_step dmc_fly_tick(const dmc_session* session, float yaw, float pitch, bool move_held, float move_dx,
                          float move_dy, bool look_held, float look_dx, float look_dy, float density, float dt) {
    namespace camera = dmcresource::camera;
    const auto move = move_held ? camera::stick_response(move_dx, move_dy, density) : camera::StickResponse{};
    const auto look = look_held ? camera::stick_response(look_dx, look_dy, density) : camera::StickResponse{};
    float speed = 1.0F;
    try {
        const std::lock_guard lock{api_mutex()};
        // Full stick crosses the framed distance in about two seconds.
        speed = std::max(1.0F, dmcresource::session_camera_distance(core(session)) * 0.5F);
    } catch (...) {}
    const auto step = camera::fly_tick(yaw, pitch, move, look, dt, speed);
    return {step.yaw, step.pitch, move.boost, move.zone};
}

void dmc_fly_leave(const dmc_session* session, float yaw, float pitch) {
    try {
        const std::lock_guard lock{api_mutex()};
        dmcresource::camera::leave_fly(yaw, pitch, dmcresource::session_camera_distance(core(session)));
    } catch (...) {}
}

void dmc_stick_knob(float dx, float dy, float density, float out[2]) {
    if (out == nullptr) return;
    const auto knob = dmcresource::camera::stick_knob(dx, dy, density);
    out[0] = knob.first;
    out[1] = knob.second;
}

bool dmc_walk(float yaw, float forward, float right, float up) {
    return dmcresource::spider::camera::walk(yaw, forward, right, up);
}

bool dmc_walk_active(void) { return dmcresource::spider::camera::walk_active(); }

dmc_finder dmc_viewfinder(const dmc_session* session, int width, int height, float zoom, bool recording,
                          int recorded_seconds) {
    dmc_finder out{};
    try {
        dmcresource::spider::camera::Finder finder;
        {
            const std::lock_guard lock{api_mutex()};
            finder = dmcresource::spider::camera::viewfinder(core(session), width, height, zoom, recording,
                                                              recorded_seconds);
        }
        out.shown = finder.shown;
        out.frame[0] = finder.frame.x;
        out.frame[1] = finder.frame.y;
        out.frame[2] = finder.frame.width;
        out.frame[3] = finder.frame.height;
        if (!finder.lines.empty()) {
            out.lines = static_cast<float*>(std::malloc(finder.lines.size() * 4U * sizeof(float)));
            if (out.lines != nullptr) {
                for (std::size_t i = 0; i < finder.lines.size(); ++i)
                    std::memcpy(out.lines + i * 4U, finder.lines[i].data(), 4U * sizeof(float));
                out.line_count = finder.lines.size();
            }
        }
        out.status = copy_string(finder.status);
    } catch (...) {
        if (out.status == nullptr) out.status = copy_string({});
    }
    return out;
}

void dmc_finder_free(dmc_finder* finder) {
    if (finder == nullptr) return;
    std::free(finder->lines);
    std::free(finder->status);
    finder->lines = nullptr;
    finder->status = nullptr;
    finder->line_count = 0;
}

void dmc_image_free(dmc_image* image) {
    if (image == nullptr) return;
    std::free(image->rgba);
    std::free(image);
}

bool dmc_export_image_size(const dmc_session* session, int* width, int* height) {
    try {
        const std::lock_guard lock{api_mutex()};
        const auto [w, h] = actions::export_image_size(core(session));
        if (width != nullptr) *width = w;
        if (height != nullptr) *height = h;
        return w > 0 && h > 0;
    } catch (...) { return false; }
}

dmc_image* dmc_render_export(const dmc_session* session) {
    try {
        const std::lock_guard lock{api_mutex()};
        if (core(session) == nullptr) return nullptr;
        return image_of(actions::render_export_image(core(session)));
    } catch (...) { return nullptr; }
}

dmc_image* dmc_render_capture(dmc_session* session, bool video, int view_width, int view_height,
                              const dmc_view* view, float motion_fps) {
    namespace cam = dmcresource::spider::camera;
    try {
        const std::lock_guard lock{api_mutex()};
        if (core(session) == nullptr || view == nullptr) return nullptr;
        // The scene moving while the shutter is open: the playing motion.
        const cam::SceneMotion motion{view->motion_frame, motion_fps};
        return image_of(cam::render_capture(core(session), video ? cam::Kind::Video : cam::Kind::Photo,
                                            view_width, view_height, view->yaw, view->pitch, view->zoom,
                                            view->render_flags, controls_of(*view), motion));
    } catch (...) { return nullptr; }
}

char* dmc_benchmark(dmc_session* session, int width, int height, const dmc_view* view, float motion_start,
                    float seconds, const char* settings) {
    dmcresource::ViewBenchmarkResult result;
    int out_width = 0, out_height = 0;
    try {
        if (view == nullptr) return copy_string("Nothing to draw.");
        const auto controls = controls_of(*view);
        float end_frame = 0.0F, loop_start = 0.0F;
        {
            const std::lock_guard lock{api_mutex()};
            if (core(session) == nullptr) return copy_string("Nothing to draw.");
            if (std::isfinite(motion_start)) {
                end_frame = dmcresource::motion::motion_end_frame(core(session));
                loop_start = dmcresource::motion::motion_loop_start_frame(core(session));
            }
        }
        dmcresource::ViewBenchmarkOptions options;
        options.seconds = std::clamp(static_cast<double>(seconds), 1.0, 120.0);
        dmcresource::frame_profile::reset();
        result = dmcresource::run_view_benchmark(options, [&](int index) {
            // The lock is taken per frame, as a render thread does, so other calls are never kept waiting
            // for the whole run.
            const std::lock_guard lock{api_mutex()};
            dmcresource::Session* s = core(session);
            if (s == nullptr) return false;
            if (std::isfinite(motion_start)) {
                // One game frame (60 per second) per drawn frame, looping like the viewer.
                float frame = motion_start + static_cast<float>(index);
                if (end_frame > 0.0F && frame > end_frame) {
                    const float span = end_frame - loop_start;
                    frame = span > 0.0F ? loop_start + std::fmod(frame - end_frame, span) : end_frame;
                }
                const dmcresource::frame_profile::Scope timed{dmcresource::frame_profile::Stage::Pose};
                (void)dmcresource::motion::apply_motion_frame(s, frame);
            }
            const auto image = dmcresource::spider::camera::render_view_frame(
                s, width, height, dmcresource::benchmark_yaw(view->yaw, index), view->pitch, view->zoom,
                view->render_flags, controls);
            if (image.pixels.empty()) return false;
            out_width = image.width;
            out_height = image.height;
            dmcresource::frame_profile::frame_done();
            return true;
        });
        // Leave the pose where the run started.
        if (std::isfinite(motion_start)) {
            const std::lock_guard lock{api_mutex()};
            if (dmcresource::Session* s = core(session)) (void)dmcresource::motion::apply_motion_frame(s, motion_start);
        }
    } catch (...) {
        result.stopped = true;
    }
    try {
        auto text = dmcresource::format_view_benchmark(result, dmcresource::view_renderer_description(), out_width,
                                                       out_height, text_or_empty(settings));
        const auto stages = dmcresource::frame_profile::per_frame_line();
        if (!stages.empty()) text += "\n" + stages;
        return copy_string(text);
    } catch (...) { return copy_string("Benchmark failed."); }
}

}  // extern "C"

/* ---- The view's touch math (#45) ----------------------------------------- */

namespace {
dmcresource::view_gesture::Orbit orbit_of(const dmc_view& v) noexcept {
    return {v.yaw, v.pitch, v.zoom, v.pan_x, v.pan_y, v.room_yaw};
}
void store(dmc_view& v, const dmcresource::view_gesture::Orbit& o) noexcept {
    v.yaw = o.yaw;
    v.pitch = o.pitch;
    v.zoom = o.zoom;
    v.pan_x = o.pan_x;
    v.pan_y = o.pan_y;
    v.room_yaw = o.room_yaw;
}
}  // namespace

extern "C" {

void dmc_view_reset(dmc_view* view) {
    if (view != nullptr) store(*view, dmcresource::view_gesture::default_orbit());
}

bool dmc_view_drag(dmc_view* view, float dx, float dy) {
    if (view == nullptr) return false;
    auto o = orbit_of(*view);
    if (!dmcresource::view_gesture::drag(o, dx, dy)) return false;
    store(*view, o);
    return true;
}

bool dmc_view_pan(dmc_view* view, float dx, float dy, int width, int height) {
    if (view == nullptr) return false;
    auto o = orbit_of(*view);
    if (!dmcresource::view_gesture::pan(o, dx, dy, width, height)) return false;
    store(*view, o);
    return true;
}

float dmc_angle_delta(float from, float to) { return dmcresource::view_gesture::angle_delta(from, to); }

bool dmc_view_twist(dmc_view* view, float radians, bool room_shown) {
    if (view == nullptr) return false;
    auto o = orbit_of(*view);
    if (!dmcresource::view_gesture::twist(o, radians, room_shown)) return false;
    store(*view, o);
    return true;
}

float dmc_pinch_zoom(float touch_zoom, float scale) { return dmcresource::view_gesture::pinch_zoom(touch_zoom, scale); }

bool dmc_fling(float velocity_x, float velocity_y, float density, dmc_spin* spin) {
    const auto s = dmcresource::view_gesture::fling(velocity_x, velocity_y, density);
    if (spin != nullptr) *spin = {s.yaw_rate, s.pitch_rate};
    return s.active();
}

bool dmc_spin_step(dmc_view* view, dmc_spin* spin, float dt) {
    if (view == nullptr || spin == nullptr) return false;
    auto o = orbit_of(*view);
    dmcresource::view_gesture::Spin s{spin->yaw_rate, spin->pitch_rate};
    const bool going = dmcresource::view_gesture::spin_step(o, s, dt);
    store(*view, o);
    *spin = {s.yaw_rate, s.pitch_rate};
    return going;
}

}  // extern "C"

/* ---- Group 3: animations ------------------------------------------------- */

extern "C" {

char* dmc_load_motion(dmc_session* session, const char* name, const uint8_t* bytes, size_t size) {
    try {
        const std::lock_guard lock{api_mutex()};
        if (core(session) == nullptr) return copy_string("Motion: no model session");
        if (bytes == nullptr || size == 0U) return copy_string("Motion: empty file");
        const auto report = dmcresource::motion::load_motion(core(session), text_or_empty(name), bytes, size);
        return copy_string(report.detail);
    } catch (...) { return copy_string("Motion: load failed"); }
}

char* dmc_load_library_motion(dmc_session* session, size_t index) {
    try {
        const std::lock_guard lock{api_mutex()};
        auto* s = core(session);
        if (s == nullptr || index >= s->motion_library.size()) return copy_string("Motion: invalid library index");
        return copy_string(dmcresource::motion::load_library_motion(s, index).detail);
    } catch (...) { return copy_string("Motion: load failed"); }
}

char* dmc_load_scripted_motion(dmc_session* session, size_t script, size_t motion) {
    try {
        const std::lock_guard lock{api_mutex()};
        return copy_string(dmcresource::motion::load_scripted_motion(core(session), script, motion).detail);
    } catch (...) { return copy_string("MotionScript: load failed"); }
}

bool dmc_motion_script_can_play(const dmc_session* session, size_t script, size_t motion) {
    try {
        const std::lock_guard lock{api_mutex()};
        return dmcresource::motion::motion_script_can_play_motion(core(session), script, motion);
    } catch (...) { return false; }
}

int dmc_motion_script_slot(const dmc_session* session, size_t script) {
    try {
        const std::lock_guard lock{api_mutex()};
        const auto slot = dmcresource::motion::motion_script_slot(core(session), script);
        return slot > static_cast<std::uint32_t>(std::numeric_limits<int>::max()) ? -1 : static_cast<int>(slot);
    } catch (...) { return -1; }
}

bool dmc_motion_range(const dmc_session* session, float* end_frame, float* loop_start) {
    try {
        const std::lock_guard lock{api_mutex()};
        if (core(session) == nullptr || !dmcresource::motion::has_motion(core(session))) return false;
        if (end_frame != nullptr) *end_frame = dmcresource::motion::motion_end_frame(core(session));
        if (loop_start != nullptr) *loop_start = dmcresource::motion::motion_loop_start_frame(core(session));
        return true;
    } catch (...) { return false; }
}

dmc_strings* dmc_class_events(const dmc_session* session) {
    try {
        const std::lock_guard lock{api_mutex()};
        auto out = std::make_unique<dmc_strings>();
        for (const char* name : dmcresource::motion::class_event_names(core(session)))
            out->items.emplace_back(name == nullptr ? "" : name);
        return out.release();
    } catch (...) { return nullptr; }
}

bool dmc_trigger_class_event(dmc_session* session, size_t index) {
    try {
        const std::lock_guard lock{api_mutex()};
        return dmcresource::motion::trigger_class_event(core(session), index);
    } catch (...) { return false; }
}

}  // extern "C"

/* ---- Menus and cards; group 4: stages, the base room, collision ---------- */

struct dmc_menu {
    std::string title, message;
    std::vector<std::string> labels, icons;
    std::vector<dmc_menu_item> items;
    std::vector<int> ready, warn;
};

struct dmc_report {
    std::string title, summary;
    std::vector<std::string> headings, lines;
    std::vector<dmc_report_group> groups;
};

struct dmc_card {
    std::string title;
    std::vector<std::pair<std::string, std::string>> rows;
    dmc_menu actions;
};

namespace {

void fill_menu(dmc_menu& out, const dmcresource::spider::actions::Menu& menu) {
    out.title = menu.title;
    out.message = menu.message;
    out.labels.clear();
    out.icons.clear();
    for (const auto& item : menu.items) {
        out.labels.push_back(item.label);
        out.icons.push_back(item.icon);
    }
    out.items.clear();
    for (std::size_t i = 0; i < menu.items.size(); ++i)
        out.items.push_back({menu.items[i].id, out.labels[i].c_str(), out.icons[i].c_str()});
}

dmcresource::StageSlot slot_of(int slot) noexcept {
    return slot == 1 ? dmcresource::StageSlot::scene : dmcresource::StageSlot::base;
}

std::uint32_t rgb_of(const std::array<std::uint8_t, 3>& c) noexcept {
    return (static_cast<std::uint32_t>(c[0]) << 16U) | (static_cast<std::uint32_t>(c[1]) << 8U) | c[2];
}

}  // namespace

extern "C" {

const char* dmc_menu_title(const dmc_menu* menu) { return menu == nullptr ? "" : menu->title.c_str(); }
const char* dmc_menu_message(const dmc_menu* menu) { return menu == nullptr ? "" : menu->message.c_str(); }
size_t dmc_menu_count(const dmc_menu* menu) { return menu == nullptr ? 0U : menu->items.size(); }
const dmc_menu_item* dmc_menu_at(const dmc_menu* menu, size_t index) {
    return menu == nullptr || index >= menu->items.size() ? nullptr : &menu->items[index];
}
bool dmc_menu_ready(const dmc_menu* menu, int id) {
    return menu != nullptr && std::find(menu->ready.begin(), menu->ready.end(), id) != menu->ready.end();
}
bool dmc_menu_warn(const dmc_menu* menu, int id) {
    return menu != nullptr && std::find(menu->warn.begin(), menu->warn.end(), id) != menu->warn.end();
}
void dmc_menu_free(dmc_menu* menu) { delete menu; }

char* dmc_room_load(const char* name, const uint8_t* bytes, size_t size) {
    try {
        if (bytes == nullptr || size == 0U) return nullptr;
        auto room = dmcresource::stage_room::build_room(text_or_empty(name), bytes, size);
        if (!room) return nullptr;
        const auto detail = room->detail;
        dmcresource::stage_room::set_current(std::move(room));
        return copy_string(detail);
    } catch (...) { return nullptr; }
}

void dmc_room_clear(void) { dmcresource::stage_room::set_current(nullptr); }
void dmc_room_set_broken(bool broken) { dmcresource::stage_room::set_broken(broken); }

size_t dmc_room_spot_count(void) {
    const auto room = dmcresource::stage_room::current();  // one snapshot (B3)
    return room ? room->spots.size() : 0U;
}

size_t dmc_room_collision_source_count(void) {
    const auto room = dmcresource::stage_room::current();
    return room ? room->collision_sources.size() : 0U;
}

bool dmc_room_animated(const dmc_session* session) {
    try {
        const std::lock_guard lock{api_mutex()};
        return dmcresource::room_animated(core(session));
    } catch (...) { return false; }
}

dmc_strings* dmc_base_spawn_points(void) {
    try {
        auto out = std::make_unique<dmc_strings>();
        for (const auto& point : dmcresource::stage_markers::spawn_points(dmcresource::StageSlot::base))
            out->items.push_back(point.title);
        return out.release();
    } catch (...) { return nullptr; }
}

bool dmc_place_at_spawn(int index) { return actions::choose_base_spawn(index); }

dmc_base_spot dmc_next_base_spot(void) {
    dmc_base_spot out{};
    out.index = -1;
    try {
        const auto next = actions::next_base_spot();
        out.spawn = next.spawn;
        out.index = next.index;
        out.count = next.count;
        out.title = copy_string(next.index < 0 ? std::string{} : next.title);
    } catch (...) {
        if (out.title == nullptr) out.title = copy_string({});
    }
    return out;
}

void dmc_base_spot_free(dmc_base_spot* spot) {
    if (spot == nullptr) return;
    std::free(spot->title);
    spot->title = nullptr;
}

dmc_room_change dmc_after_new_room(const char* room, const char* effects, const char* config) {
    dmc_room_change out{};
    try {
        const auto change = actions::after_new_room(text_or_empty(room), text_or_empty(effects), text_or_empty(config));
        out.keep_effects = change.keep_effects;
        out.keep_config = change.keep_config;
        out.find_effects = copy_string(change.find_effects);
        out.find_config = copy_string(change.find_config);
    } catch (...) {}
    if (out.find_effects == nullptr) out.find_effects = copy_string({});
    if (out.find_config == nullptr) out.find_config = copy_string({});
    return out;
}

void dmc_room_change_free(dmc_room_change* change) {
    if (change == nullptr) return;
    std::free(change->find_effects);
    std::free(change->find_config);
    change->find_effects = nullptr;
    change->find_config = nullptr;
}

char* dmc_preset_label(const char* room) {
    try { return copy_string(actions::preset_label(text_or_empty(room))); } catch (...) { return copy_string({}); }
}

dmc_menu* dmc_base_room_menu(bool room_loaded) {
    try {
        const std::lock_guard lock{api_mutex()};
        const auto menu = actions::base_room_menu({.root_scene = true, .room_loaded = room_loaded, .room_shown = true});
        auto out = std::make_unique<dmc_menu>();
        fill_menu(*out, {.title = {}, .message = {}, .items = menu.stage_ops});
        out->ready = menu.stage_ops_ready;
        out->warn = menu.stage_ops_warn;
        return out.release();
    } catch (...) { return nullptr; }
}

char* dmc_room_effects_load(const char* name, const uint8_t* bytes, size_t size, int slot) {
    try {
        if (bytes == nullptr || size == 0U) return nullptr;
        auto host = dmcresource::stage_room::make_effect_host(text_or_empty(name), bytes, size);
        if (!host || host->effect_banks.empty()) return nullptr;
        const std::string detail = std::to_string(host->effect_banks.front().bank.records.size()) +
                                   " effect records, " +
                                   std::to_string(host->effect_banks.front().textures.size()) + " textures";
        dmcresource::stage_room::set_effect_host(slot_of(slot), std::move(host));
        return copy_string(detail);
    } catch (...) { return nullptr; }
}

void dmc_room_effects_clear(int slot) { dmcresource::stage_room::set_effect_host(slot_of(slot), nullptr); }

dmc_outcome dmc_stage_config_load(const char* name, const uint8_t* bytes, size_t size, int slot) {
    try {
        // Into the base room's set or the opened stage's (stage_slot.h).
        dmcresource::set_active_slot(slot_of(slot));
        const std::lock_guard lock{api_mutex()};
        return outcome_of(actions::load_stage_config(text_or_empty(name), bytes, bytes == nullptr ? 0U : size));
    } catch (...) { return outcome_of({}); }
}

void dmc_stage_config_clear(int slot) { dmcresource::stage_markers::set_current(slot_of(slot), nullptr); }

bool dmc_stage_archives(const char* stage_name, char** effects, char** config) {
    try {
        const auto archives = dmcresource::stage_markers::stage_archives(text_or_empty(stage_name));
        if (!archives) return false;
        if (effects != nullptr) *effects = copy_string(archives->effects);
        if (config != nullptr) *config = copy_string(archives->config);
        return true;
    } catch (...) { return false; }
}

int dmc_stage_slot(const dmc_session* session) {
    try {
        const std::lock_guard lock{api_mutex()};
        return dmcresource::slot_for(core(session)) == dmcresource::StageSlot::scene ? 1 : 0;
    } catch (...) { return 0; }
}

bool dmc_stage_start_view(dmc_session* session, float out[3]) {
    try {
        const std::lock_guard lock{api_mutex()};
        const auto view = actions::stage_start_view(core(session));
        if (!view) return false;
        if (out != nullptr) for (int i = 0; i < 3; ++i) out[i] = (*view)[static_cast<std::size_t>(i)];
        return true;
    } catch (...) { return false; }
}

char* dmc_stage_stem(const dmc_session* session) {
    try {
        const std::lock_guard lock{api_mutex()};
        const auto* s = core(session);
        if (s == nullptr || s->stage == nullptr) return copy_string({});
        int number = dmcresource::stage_markers::stage_number(s->stage->name);
        if (number < 0) number = dmcresource::stage_markers::stage_number(s->source_name);
        if (number < 0 || number > 999) return copy_string({});
        char stem[8];
        std::snprintf(stem, sizeof stem, "st%03d", number);
        return copy_string(stem);
    } catch (...) { return copy_string({}); }
}

size_t dmc_stage_collision_source_count(const dmc_session* session) {
    try {
        const std::lock_guard lock{api_mutex()};
        const auto* s = core(session);
        return s != nullptr && s->stage != nullptr ? s->stage->collision_sources.size() : 0U;
    } catch (...) { return 0U; }
}

size_t dmc_collision_kinds(const dmc_session* session, dmc_collision_kind* out, size_t capacity) {
    try {
        const std::lock_guard lock{api_mutex()};
        const auto kinds = actions::shown_hits_kinds(core(session));
        for (std::size_t i = 0; out != nullptr && i < kinds.size() && i < capacity; ++i) {
            out[i] = {kinds[i].flags, kinds[i].count, kinds[i].floors, kinds[i].walls, kinds[i].ceilings,
                      rgb_of(dmcresource::collision_class_color(dmcresource::collision_color_class(kinds[i].flags)))};
        }
        return kinds.size();
    } catch (...) { return 0U; }
}

size_t dmc_collision_attack_ids(const dmc_session* session, int* out, size_t capacity) {
    try {
        const std::lock_guard lock{api_mutex()};
        const auto* s = core(session);
        if (s == nullptr) return 0U;
        const auto ids = dmcresource::collision::collision_attack_ids(*s);
        for (std::size_t i = 0; out != nullptr && i < ids.size() && i < capacity; ++i) out[i] = ids[i];
        return ids.size();
    } catch (...) { return 0U; }
}

char* dmc_select_collision_attack(dmc_session* session, int attack) {
    try {
        const std::lock_guard lock{api_mutex()};
        auto* s = core(session);
        if (!dmcresource::collision::select_collision_attack(s, attack)) return copy_string({});
        return copy_string(dmcresource::collision::describe_collision_selection(*s));
    } catch (...) { return copy_string({}); }
}

dmc_report* dmc_collision_report(const dmc_session* session, bool attack) {
    try {
        const std::lock_guard lock{api_mutex()};
        const auto report = attack ? actions::attack_collision_report(core(session))
                                   : actions::stage_collision_report(core(session));
        auto out = std::make_unique<dmc_report>();
        out->title = report.title;
        out->summary = report.summary;
        for (const auto& group : report.groups) {
            out->headings.push_back(group.heading);
            std::string lines;
            for (std::size_t i = 0; i < group.lines.size(); ++i) {
                if (i > 0U) lines.push_back('\n');
                lines += group.lines[i];
            }
            out->lines.push_back(std::move(lines));
        }
        for (std::size_t i = 0; i < report.groups.size(); ++i) {
            const auto& group = report.groups[i];
            out->groups.push_back({group.coloured ? static_cast<std::int64_t>(rgb_of(group.colour)) : -1,
                                   out->headings[i].c_str(), out->lines[i].c_str()});
        }
        return out.release();
    } catch (...) { return nullptr; }
}

const char* dmc_report_title(const dmc_report* report) { return report == nullptr ? "" : report->title.c_str(); }
const char* dmc_report_summary(const dmc_report* report) { return report == nullptr ? "" : report->summary.c_str(); }
size_t dmc_report_group_count(const dmc_report* report) { return report == nullptr ? 0U : report->groups.size(); }
const dmc_report_group* dmc_report_group_at(const dmc_report* report, size_t index) {
    return report == nullptr || index >= report->groups.size() ? nullptr : &report->groups[index];
}
void dmc_report_free(dmc_report* report) { delete report; }

dmc_card* dmc_marker_card(int marker) {
    try {
        const std::lock_guard lock{api_mutex()};
        const auto card = actions::marker_card(marker);
        if (card.title.empty()) return nullptr;
        auto out = std::make_unique<dmc_card>();
        out->title = card.title;
        out->rows = card.rows;
        fill_menu(out->actions, {.title = card.title, .message = {}, .items = card.actions});
        return out.release();
    } catch (...) { return nullptr; }
}

const char* dmc_card_title(const dmc_card* card) { return card == nullptr ? "" : card->title.c_str(); }
size_t dmc_card_row_count(const dmc_card* card) { return card == nullptr ? 0U : card->rows.size(); }
bool dmc_card_row(const dmc_card* card, size_t index, const char** name, const char** value) {
    if (card == nullptr || index >= card->rows.size()) return false;
    if (name != nullptr) *name = card->rows[index].first.c_str();
    if (value != nullptr) *value = card->rows[index].second.c_str();
    return true;
}
const dmc_menu* dmc_card_actions(const dmc_card* card) { return card == nullptr ? nullptr : &card->actions; }
void dmc_card_free(dmc_card* card) { delete card; }

dmc_outcome dmc_run_card_action(int marker, int id, dmc_session** opened) {
    if (opened != nullptr) *opened = nullptr;
    try {
        const std::lock_guard lock{api_mutex()};
        std::unique_ptr<dmcresource::Session> session;
        const auto outcome = actions::run_marker_card_action(marker, id, &session);
        if (opened != nullptr && session) *opened = wrap(std::move(session));
        return outcome_of(outcome);
    } catch (...) { return outcome_of({}); }
}

}  // extern "C"

/* ---- Group 5: the companion menu, effects, sounds ------------------------ */

struct dmc_companion {
    dmc_menu parts[4];
    std::vector<int> dimmed;
};

extern "C" {

dmc_companion* dmc_companion_menu(const dmc_session* session, const dmc_shell_facts* facts) {
    try {
        const dmc_shell_facts f = facts != nullptr ? *facts : dmc_shell_facts{};
        const std::lock_guard lock{api_mutex()};
        const auto menu = actions::companion_menu(core(session), {.root_scene = f.root_scene,
                                                                  .assembled_pac_source = f.assembled_pac_source,
                                                                  .stage_source = f.stage_source,
                                                                  .room_loaded = f.room_loaded,
                                                                  .room_shown = f.room_shown,
                                                                  .recording = f.recording});
        auto out = std::make_unique<dmc_companion>();
        fill_menu(out->parts[DMC_COMPANION_SHORTCUTS], {.title = {}, .message = {}, .items = menu.shortcuts});
        fill_menu(out->parts[DMC_COMPANION_ROWS], {.title = {}, .message = {}, .items = menu.rows});
        fill_menu(out->parts[DMC_COMPANION_MORE], {.title = {}, .message = {}, .items = menu.more});
        fill_menu(out->parts[DMC_COMPANION_STAGE_OPS], {.title = {}, .message = {}, .items = menu.stage_ops});
        out->parts[DMC_COMPANION_STAGE_OPS].ready = menu.stage_ops_ready;
        out->parts[DMC_COMPANION_STAGE_OPS].warn = menu.stage_ops_warn;
        out->dimmed = menu.dimmed;
        return out.release();
    } catch (...) { return nullptr; }
}

const dmc_menu* dmc_companion_get(const dmc_companion* menu, int part) {
    return menu == nullptr || part < 0 || part > 3 ? nullptr : &menu->parts[part];
}

bool dmc_companion_dimmed(const dmc_companion* menu, int id) {
    return menu != nullptr && std::find(menu->dimmed.begin(), menu->dimmed.end(), id) != menu->dimmed.end();
}

void dmc_companion_free(dmc_companion* menu) { delete menu; }

dmc_outcome dmc_run_companion_action(dmc_session* session, int id, dmc_session** opened) {
    if (opened != nullptr) *opened = nullptr;
    try {
        const std::lock_guard lock{api_mutex()};
        std::unique_ptr<dmcresource::Session> made;
        const auto outcome = actions::run_companion_action(core(session), id, &made);
        if (opened != nullptr && made) *opened = wrap(std::move(made));
        return outcome_of(outcome);
    } catch (...) { return outcome_of({}); }
}

dmc_menu* dmc_effect_menu(const dmc_session* session) {
    try {
        const std::lock_guard lock{api_mutex()};
        auto out = std::make_unique<dmc_menu>();
        fill_menu(*out, actions::effect_menu(core(session)));
        return out.release();
    } catch (...) { return nullptr; }
}

dmc_outcome dmc_play_effect(dmc_session* session, int id) {
    try {
        const std::lock_guard lock{api_mutex()};
        return outcome_of(actions::play_effect(core(session), id));
    } catch (...) { return outcome_of({}); }
}

dmc_menu* dmc_sound_menu(const dmc_session* session) {
    try {
        const std::lock_guard lock{api_mutex()};
        auto out = std::make_unique<dmc_menu>();
        fill_menu(*out, actions::sound_menu(core(session)));
        return out.release();
    } catch (...) { return nullptr; }
}

dmc_pcm dmc_sound_pcm(const dmc_session* session, int index) {
    dmc_pcm out{};
    try {
        const std::lock_guard lock{api_mutex()};
        const auto pcm = actions::sound_pcm(core(session), index);
        out.sample_rate = pcm.sample_rate;
        if (!pcm.samples.empty()) {
            out.samples = static_cast<int16_t*>(std::malloc(pcm.samples.size() * sizeof(int16_t)));
            if (out.samples != nullptr) {
                std::memcpy(out.samples, pcm.samples.data(), pcm.samples.size() * sizeof(int16_t));
                out.sample_count = pcm.samples.size();
            }
        }
        out.message = copy_string(pcm.message);
    } catch (...) {}
    if (out.message == nullptr) out.message = copy_string({});
    return out;
}

void dmc_pcm_free(dmc_pcm* pcm) {
    if (pcm == nullptr) return;
    std::free(pcm->samples);
    std::free(pcm->message);
    pcm->samples = nullptr;
    pcm->message = nullptr;
    pcm->sample_count = 0;
}

}  // extern "C"

/* ---- Group 6: cutscenes; group 7: video ---------------------------------- */

extern "C" {

bool dmc_camera_follows_cuts(const dmc_session* session) {
    try {
        const std::lock_guard lock{api_mutex()};
        return dmcresource::spider::camera::follows_cuts(core(session));
    } catch (...) { return false; }
}

bool dmc_cutscene_audio_format(const dmc_session* session, uint32_t* sample_rate, uint32_t* channels) {
    try {
        const std::lock_guard lock{api_mutex()};
        const auto format = dmcresource::demo::audio_format(core(session));
        if (!format) return false;
        if (sample_rate != nullptr) *sample_rate = format->sample_rate;
        if (channels != nullptr) *channels = format->channels;
        return true;
    } catch (...) { return false; }
}

int64_t dmc_cutscene_audio_position(const dmc_session* session, float frame) {
    try {
        const std::lock_guard lock{api_mutex()};
        return dmcresource::demo::audio_position(core(session), frame);
    } catch (...) { return 0; }
}

size_t dmc_cutscene_audio_read(const dmc_session* session, int64_t position, size_t frames, int16_t* out,
                               size_t capacity) {
    try {
        std::vector<std::int16_t> samples;
        {
            const std::lock_guard lock{api_mutex()};
            samples = dmcresource::demo::audio_read(core(session), position, frames);
        }
        if (out == nullptr || samples.empty()) return 0U;
        const std::size_t n = std::min(samples.size(), capacity);
        std::memcpy(out, samples.data(), n * sizeof(int16_t));
        return n;
    } catch (...) { return 0U; }
}

bool dmc_cutscene_subtitle(const dmc_session* session, dmc_subtitle* out) {
    if (out == nullptr) return false;
    *out = dmc_subtitle{};
    try {
        const std::lock_guard lock{api_mutex()};
        const auto* s = core(session);
        const auto shown = s != nullptr ? dmcresource::demo::subtitle(*s) : std::nullopt;
        if (!shown) return false;
        std::string text;
        for (const auto& line : shown->lines) {
            if (!text.empty()) text.push_back('\n');
            text += line;
        }
        const auto l = dmcresource::messages::layout(*shown);
        out->text = copy_string(text);
        out->centre_x = l.centre_x;
        out->top = l.top;
        out->size = l.size;
        out->alpha = l.alpha;
        for (int i = 0; i < 3; ++i) {
            out->front[i] = l.front[static_cast<std::size_t>(i)];
            out->back[i] = l.back[static_cast<std::size_t>(i)];
        }
        return out->text != nullptr;
    } catch (...) { return false; }
}

void dmc_subtitle_free(dmc_subtitle* subtitle) {
    if (subtitle == nullptr) return;
    std::free(subtitle->text);
    subtitle->text = nullptr;
}

bool dmc_cutscene_take_camera(dmc_session* session, float out[3]) {
    try {
        const std::lock_guard lock{api_mutex()};
        const auto view = dmcresource::demo::take_camera(core(session));
        if (!view) return false;
        dmcresource::camera::enter_fly(view->eye);
        if (out != nullptr) {
            out[0] = view->yaw;
            out[1] = view->pitch;
            out[2] = view->zoom;
        }
        return true;
    } catch (...) { return false; }
}

int dmc_cutscene_vibration(const dmc_session* session, float frame) {
    try {
        const std::lock_guard lock{api_mutex()};
        const auto* s = core(session);
        return s != nullptr ? dmcresource::demo::vibration(*s, frame) : 0;
    } catch (...) { return 0; }
}

dmc_outcome dmc_cutscene_sound_load(dmc_session* session, const char* name, const uint8_t* bytes, size_t size) {
    try {
        const std::lock_guard lock{api_mutex()};
        return outcome_of(actions::load_cutscene_sound(core(session), text_or_empty(name), bytes,
                                                       bytes == nullptr ? 0U : size));
    } catch (...) { return outcome_of({}); }
}

void dmc_video_begin(void) {
    try { dmcresource::spider::camera::begin_video(); } catch (...) {}
}

bool dmc_video_frame_size(int view_width, int view_height, int* width, int* height) {
    namespace cam = dmcresource::spider::camera;
    const auto p = cam::plan(cam::Kind::Video, view_width, view_height);
    if (width != nullptr) *width = p.width;
    if (height != nullptr) *height = p.height;
    return p.width > 0 && p.height > 0;
}

int dmc_video_frame(dmc_session* session, int view_width, int view_height, const dmc_view* view, uint8_t* rgba,
                    size_t capacity) {
    namespace cam = dmcresource::spider::camera;
    try {
        const std::lock_guard lock{api_mutex()};
        auto* s = core(session);
        if (s == nullptr || view == nullptr || rgba == nullptr) return 0;
        int status = 1;
        if (std::isfinite(view->motion_frame) && !dmcresource::motion::apply_motion_frame(s, view->motion_frame)) {
            status = 2;
        }
        const auto p = cam::plan(cam::Kind::Video, view_width, view_height);
        const auto image = cam::render_capture(s, cam::Kind::Video, view_width, view_height, view->yaw, view->pitch,
                                               view->zoom, view->render_flags, controls_of(*view));
        if (image.width != p.width || image.height != p.height || image.pixels.size() > capacity ||
            image.pixels.empty()) {
            return 0;
        }
        std::memcpy(rgba, image.pixels.data(), image.pixels.size());
        return status;
    } catch (...) { return 0; }
}

}  // extern "C"

/* ---- Group 8: the photo camera ------------------------------------------- */

struct dmc_camera_settings {
    std::vector<std::vector<const char*>> option_ptrs;
    std::vector<dmc_camera_setting> rows;
};

struct dmc_camera_dials {
    std::vector<dmcresource::spider::camera::Dial> dials;
    std::vector<std::vector<int>> with;
    std::vector<dmc_camera_dial> rows;
};

extern "C" {

dmc_camera_settings* dmc_camera_settings_list(void) {
    try {
        const auto& list = dmcresource::spider::camera::settings();  // static for the program's life
        auto out = std::make_unique<dmc_camera_settings>();
        out->option_ptrs.resize(list.size());
        for (std::size_t i = 0; i < list.size(); ++i) {
            for (const auto& option : list[i].options) out->option_ptrs[i].push_back(option.c_str());
            out->rows.push_back({static_cast<int>(list[i].key), list[i].id.c_str(), list[i].section.c_str(),
                                 list[i].label.c_str(), list[i].hint.c_str(), out->option_ptrs[i].size(),
                                 out->option_ptrs[i].data(), list[i].start});
        }
        return out.release();
    } catch (...) { return nullptr; }
}

size_t dmc_camera_settings_count(const dmc_camera_settings* list) { return list == nullptr ? 0U : list->rows.size(); }
const dmc_camera_setting* dmc_camera_settings_at(const dmc_camera_settings* list, size_t index) {
    return list == nullptr || index >= list->rows.size() ? nullptr : &list->rows[index];
}
void dmc_camera_settings_free(dmc_camera_settings* list) { delete list; }

void dmc_camera_set_choices(const int* choices, size_t count) {
    try {
        std::vector<int> values;
        if (choices != nullptr) values.assign(choices, choices + count);
        dmcresource::spider::camera::set_choices(values);
    } catch (...) {}
}

dmc_camera_dials* dmc_camera_dials_list(void) {
    try {
        auto out = std::make_unique<dmc_camera_dials>();
        out->dials = dmcresource::spider::camera::dials();
        for (const auto& dial : out->dials) {
            std::vector<int> with;
            for (const auto key : dial.with) with.push_back(static_cast<int>(key));
            out->with.push_back(std::move(with));
        }
        for (std::size_t i = 0; i < out->dials.size(); ++i) {
            out->rows.push_back({static_cast<int>(out->dials[i].key), out->dials[i].title.c_str(),
                                 out->dials[i].value.c_str(), out->with[i].size(), out->with[i].data()});
        }
        return out.release();
    } catch (...) { return nullptr; }
}

size_t dmc_camera_dials_count(const dmc_camera_dials* list) { return list == nullptr ? 0U : list->rows.size(); }
const dmc_camera_dial* dmc_camera_dials_at(const dmc_camera_dials* list, size_t index) {
    return list == nullptr || index >= list->rows.size() ? nullptr : &list->rows[index];
}
void dmc_camera_dials_free(dmc_camera_dials* list) { delete list; }

bool dmc_camera_follow_up_of(int key, int option, dmc_camera_follow_up* out) {
    namespace cam = dmcresource::spider::camera;
    if (out == nullptr) return false;
    *out = dmc_camera_follow_up{};
    try {
        if (key < 0 || key >= static_cast<int>(cam::Key::Count)) return false;
        const auto follow = cam::follow_up(static_cast<cam::Key>(key), option);
        if (follow.changes.empty()) return false;
        out->message = copy_string(follow.message);
        out->keys = static_cast<int*>(std::malloc(follow.changes.size() * sizeof(int)));
        out->options = static_cast<int*>(std::malloc(follow.changes.size() * sizeof(int)));
        if (out->keys == nullptr || out->options == nullptr) { dmc_camera_follow_up_free(out); return false; }
        for (std::size_t i = 0; i < follow.changes.size(); ++i) {
            out->keys[i] = static_cast<int>(follow.changes[i].key);
            out->options[i] = follow.changes[i].option;
        }
        out->count = follow.changes.size();
        return true;
    } catch (...) { dmc_camera_follow_up_free(out); return false; }
}

void dmc_camera_follow_up_free(dmc_camera_follow_up* follow_up) {
    if (follow_up == nullptr) return;
    std::free(follow_up->message);
    std::free(follow_up->keys);
    std::free(follow_up->options);
    *follow_up = dmc_camera_follow_up{};
}

dmc_camera_plan dmc_camera_plan_of(bool video, int view_width, int view_height) {
    namespace cam = dmcresource::spider::camera;
    const auto p = cam::plan(video ? cam::Kind::Video : cam::Kind::Photo, view_width, view_height);
    return {p.width, p.height, p.format, p.quality, p.fps, p.keyframe_ms, p.limit_seconds, p.timer_seconds,
            p.burst, p.subtitles};
}

char* dmc_camera_focus_at(const dmc_session* session, int width, int height, const dmc_view* view, float x, float y) {
    namespace cam = dmcresource::spider::camera;
    try {
        if (view == nullptr) return copy_string({});
        const std::lock_guard lock{api_mutex()};
        const float units = cam::focus_at(core(session), width, height, view->yaw, view->pitch, view->zoom,
                                          view->render_flags, controls_of(*view), x, y);
        return copy_string(cam::focus_message(units));
    } catch (...) { return copy_string({}); }
}

char* dmc_camera_focus_lock(void) {
    namespace cam = dmcresource::spider::camera;
    try {
        const bool locked = cam::toggle_focus_lock();
        return copy_string(locked ? "AF lock: " + cam::focus_message(cam::focus_point()) : std::string{"Auto focus"});
    } catch (...) { return copy_string({}); }
}

char* dmc_camera_focus_ring(float steps) {
    namespace cam = dmcresource::spider::camera;
    try { return copy_string(cam::focus_message(cam::turn_focus_ring(steps))); } catch (...) { return copy_string({}); }
}

bool dmc_camera_polarizer_turnable(void) { return dmcresource::spider::camera::polarizer_turnable(); }
float dmc_camera_polarizer_turn(float radians) { return dmcresource::spider::camera::turn_polarizer(radians); }
void dmc_camera_set_polarizer(float radians) { dmcresource::spider::camera::set_polarizer_angle(radians); }

bool dmc_capture_dng(dmc_session* session, int view_width, int view_height, const dmc_view* view, float motion_fps,
                     int fd, dmc_image** developed) {
    namespace cam = dmcresource::spider::camera;
    if (developed != nullptr) *developed = nullptr;
    try {
        const std::lock_guard lock{api_mutex()};
        auto* s = core(session);
        if (s == nullptr || view == nullptr || fd < 0) return false;
        dmcresource::RgbaImage image;
        const bool ok = cam::capture_raw(s, view_width, view_height, view->yaw, view->pitch, view->zoom,
                                         view->render_flags, controls_of(*view), fd,
                                         developed != nullptr ? &image : nullptr,
                                         cam::SceneMotion{view->motion_frame, motion_fps});
        if (!ok) return false;
        if (developed != nullptr) {
            *developed = image_of(image);
            if (*developed == nullptr) return false;
        }
        return true;
    } catch (...) { return false; }
}

}  // extern "C"

/* ---- Group 9: diagnostics, crash notes, texture re-encoding -------------- */

#ifndef DMC_NATIVE_READER_BUILD_SHA
#define DMC_NATIVE_READER_BUILD_SHA "unknown"
#endif

extern "C" {

char* dmc_build_identity(void) { return copy_string(DMC_NATIVE_READER_BUILD_SHA); }

char* dmc_benchmark_settings(const char* const* keys, const float* values, size_t count) {
    try {
        settings::Stored stored;
        for (size_t index = 0; keys != nullptr && values != nullptr && index < count; ++index) {
            stored.keys.push_back(text_or_empty(keys[index]));
            stored.values.push_back(values[index]);
        }
        return copy_string(settings::benchmark_summary(settings::resolve(stored)));
    } catch (...) { return copy_string({}); }
}

char* dmc_diagnostics(const dmc_session* session) {
    try {
        const std::lock_guard lock{api_mutex()};
        return copy_string(dmcresource::describe_session_diagnostics(core(session)));
    } catch (...) { return copy_string({}); }
}

char* dmc_inspect(const dmc_session* session, int topic) {
    try {
        if (topic < 1 || topic > 3) return copy_string("Information unavailable");
        const std::lock_guard lock{api_mutex()};
        const auto document =
            dmcresource::inspect_session(core(session), static_cast<dmcresource::InspectionTopic>(topic));
        return copy_string(dmcresource::format_inspection_tree(document));
    } catch (...) { return copy_string("Information unavailable"); }
}

void dmc_crash_install(const char* path) {
    try { dmcresource::crash_recorder::install(text_or_empty(path)); } catch (...) {}
}

void dmc_crash_context(const char* text) {
    try { dmcresource::crash_recorder::set_context(text_or_empty(text)); } catch (...) {}
}

void dmc_test_crash(void) { std::abort(); }

dmc_strings* dmc_texture_format_names(void) {
    try {
        auto out = std::make_unique<dmc_strings>();
        for (const auto& c : actions::texture_format_choices()) out->items.push_back(c.name);
        return out.release();
    } catch (...) { return nullptr; }
}

dmc_strings* dmc_texture_format_labels(void) {
    try {
        auto out = std::make_unique<dmc_strings>();
        for (const auto& c : actions::texture_format_choices()) out->items.push_back(c.label);
        return out.release();
    } catch (...) { return nullptr; }
}

dmc_session* dmc_reencode_textures(const dmc_session* session, const char* format, bool force_dx10, char** detail) {
    if (detail != nullptr) *detail = nullptr;
    try {
        const std::lock_guard lock{api_mutex()};
        std::string text;
        auto result = actions::reencode_textures(core(session), text_or_empty(format), force_dx10, &text);
        if (detail != nullptr) *detail = copy_string(text);
        return wrap(std::move(result));
    } catch (...) {
        if (detail != nullptr && *detail == nullptr) *detail = copy_string("Re-encoding failed");
        return nullptr;
    }
}

int dmc_api_version(void) { return DMC_API_VERSION; }

}  // extern "C"
