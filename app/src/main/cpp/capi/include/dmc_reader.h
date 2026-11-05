/*
 * DMC Native Reader: the C API over the core, for shells that are not
 * Android (the Swift shells for macOS and iOS import it through
 * module.modulemap). It mirrors what the Android shell asks the core over
 * JNI: every decision and every line of text is the core's; a shell reads
 * files, draws what it is given and stores the user's choices.
 *
 * Strings returned as `char*` are the caller's and go back through
 * dmc_string_free. Lists (settings, the motion strip) are owned by their
 * handle and stay valid until it is freed. A session is used by one thread
 * at a time; the API serialises its own calls.
 */
#ifndef DMC_READER_H
#define DMC_READER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct dmc_session dmc_session;

void dmc_string_free(char* text);

/* ---- Opening ----------------------------------------------------------- */

typedef enum dmc_opened_as {
    DMC_OPENED_REJECTED = 0,     /* no module accepted the file; title says why */
    DMC_OPENED_PLAIN = 1,
    DMC_OPENED_ASSEMBLED_PAC = 2,
    DMC_OPENED_STAGE = 3
} dmc_opened_as;

typedef struct dmc_open_result {
    dmc_session* session;        /* NULL when rejected */
    dmc_opened_as opened_as;
    char* title;                 /* free with dmc_string_free */
    bool room_effects;           /* a stage effect bank: also the room's effects */
} dmc_open_result;

/* How a picked file opens, decided by the core (spider open_resource). */
dmc_open_result dmc_open(const char* name, const uint8_t* bytes, size_t size);
void dmc_close(dmc_session* session);

size_t dmc_child_count(const dmc_session* session);
char* dmc_child_title(const dmc_session* session, size_t index);
dmc_session* dmc_open_child(const dmc_session* session, size_t index);

/* Black Widow state bits (spider/black_widow.h): what the session can do. */
uint64_t dmc_state_flags(const dmc_session* session);

/* ---- Text ---------------------------------------------------------------- */

char* dmc_welcome_text(const char* app_version, const char* native_build);

typedef struct dmc_staged_file {
    const char* role;
    const char* name;
} dmc_staged_file;

/* The open session's Info (spider session_info_text). */
char* dmc_info_text(const dmc_session* session, const char* title, size_t navigation_depth,
                    const char* back_title, const dmc_staged_file* staged, size_t staged_count);

/* File health: returns 0 fine, 1 notice (orange), 2 problem (red); *report
 * (when given) receives the text for the Info window. */
int dmc_file_health(const dmc_session* session, const char* filename, char** report);

/* ---- The view ------------------------------------------------------------ */

typedef struct dmc_view {
    float yaw;
    float pitch;
    float zoom;
    uint32_t render_flags;       /* dmc_apply_settings' flags plus the view's own bits */
    float pan_x;
    float pan_y;
    float room_yaw;
    bool follow;
    float dolly;
    float motion_frame;          /* NaN: leave the pose as it is */
} dmc_view;

/* A texture's own picture (a DDS, a PTX's chosen texture): its size, 0 x 0
 * when the session shows none; then its RGBA8 rows into `rgba`. A session
 * with a picture is shown as it, not through dmc_render. */
bool dmc_image_preview_size(const dmc_session* session, int* width, int* height);
bool dmc_image_preview(const dmc_session* session, uint8_t* rgba, size_t capacity);

/* Draws a frame into `rgba` (width * height * 4 bytes, rows top down).
 * Returns 0 on failure, 1 drawn, 2 drawn but the motion could not be posed. */
int dmc_render(dmc_session* session, int width, int height, const dmc_view* view, uint8_t* rgba,
               size_t capacity);

/* ---- Settings (spider/viewer_settings.h) --------------------------------- */

typedef enum dmc_setting_control {
    DMC_SETTING_CHOICE = 0,
    DMC_SETTING_SLIDER = 1,
    DMC_SETTING_SLOT = 2         /* a piece the shell draws itself; key names it */
} dmc_setting_control;

typedef enum dmc_setting_storage {
    DMC_STORE_BOOL = 0,
    DMC_STORE_INT = 1,
    DMC_STORE_FLOAT = 2
} dmc_setting_storage;

typedef struct dmc_setting_row {
    const char* key;
    const char* section;
    const char* label;
    int control;                 /* dmc_setting_control */
    int storage;                 /* dmc_setting_storage */
    size_t choice_count;
    const char* const* choices;
    const float* values;         /* the stored value of each choice */
    float fallback;              /* the stored value when nothing is stored */
    const char* hint;
    bool render_now;
    bool room_only;
} dmc_setting_row;

typedef struct dmc_settings dmc_settings;

dmc_settings* dmc_settings_schema(bool describe_device);
size_t dmc_settings_count(const dmc_settings* settings);
const dmc_setting_row* dmc_settings_row(const dmc_settings* settings, size_t index);
void dmc_settings_free(dmc_settings* settings);
size_t dmc_settings_selected(const dmc_setting_row* row, float stored);

typedef struct dmc_applied_settings {
    uint32_t render_flags;
    int max_side;
    int frame_ms;
    float speed;
    bool shadows;
    bool fast_preview;
    int mesh_line;
    int collision_line;
} dmc_applied_settings;

/* Sets the core's GPU state from the stored values; the rest is the view's. */
dmc_applied_settings dmc_apply_settings(const char* const* keys, const float* values, size_t count);

/* ---- The animation strip (spider/motion_strip.h) ------------------------- */

typedef struct dmc_script_button {
    int script_index;
    int slot;
    const char* label;
    const char* description;
} dmc_script_button;

typedef struct dmc_motion_card {
    const char* name;
    int library_index;
    int staged_index;
    int pack_slot;
    bool separator_before;
    const char* separator_description;
    const char* kind;
    const char* stem;
    const char* description;
    size_t script_count;
    const dmc_script_button* scripts;
} dmc_motion_card;

typedef struct dmc_motion_strip dmc_motion_strip;

dmc_motion_strip* dmc_motion_strip_build(const dmc_session* session, bool with_library,
                                         const char* const* staged, size_t staged_count);
size_t dmc_motion_strip_count(const dmc_motion_strip* strip);
const dmc_motion_card* dmc_motion_strip_card(const dmc_motion_strip* strip, size_t index);
void dmc_motion_strip_free(dmc_motion_strip* strip);

#ifdef __cplusplus
}
#endif

#endif /* DMC_READER_H */
