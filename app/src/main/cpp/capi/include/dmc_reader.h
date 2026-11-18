/*
 * DMC Native Reader: the C API over the core, for shells that are not
 * Android (the Swift shells for macOS and iOS import it through
 * module.modulemap). It mirrors what the Android shell asks the core over
 * JNI: every decision and every line of text is the core's; a shell reads
 * files, draws what it is given and stores the user's choices.
 *
 * Strings returned as `char*` are the caller's and go back through
 * dmc_string_free. Lists (settings, the motion strip) are owned by their
 * handle and stay valid until it is freed.
 *
 * Threads: every call that takes a session holds the core's session lock
 * for its whole use (the JNI shell takes the same lock), so a frame drawn on
 * a render thread and a change made on the UI thread never overlap. A call
 * waits while a frame is being drawn. The stage room and the camera are the
 * core's own state, not a session's, and have their own locks.
 *
 * Ownership: a session from dmc_open, dmc_open_source, dmc_open_child,
 * dmc_assemble, dmc_compose or dmc_open_uv_gallery is the caller's and is
 * closed once with dmc_close. A session built from others (a child, an
 * assembly, a composition) owns copies of what it needs: its sources may be
 * closed before it.
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

/* A file opened as a source for dmc_assemble or dmc_compose: the file
 * itself, without open_resource's decisions (no stage fallback, no PAC
 * assembly). NULL when no module accepts it. */
dmc_session* dmc_open_source(const char* name, const uint8_t* bytes, size_t size);

/* ---- Lists of strings ---------------------------------------------------- */

typedef struct dmc_strings dmc_strings;

size_t dmc_strings_count(const dmc_strings* list);
const char* dmc_strings_at(const dmc_strings* list, size_t index);  /* NULL past the end */
void dmc_strings_free(dmc_strings* list);

/* ---- Assembling and composing (spider session actions) ------------------ */

/* A character's PACs as one scene (pac_assembly): `archives` are sources,
 * `names` their file names; `enemy_variant` picks the class of an archive
 * shared by several (dmc_variant_names lists them). NULL when nothing could
 * be assembled. */
dmc_session* dmc_assemble(const dmc_session* const* archives, const char* const* names, size_t count,
                          size_t enemy_variant);

/* The classes an archive holds, by its file name (em000.pac: CEm000...);
 * empty for an archive of one class. */
dmc_strings* dmc_variant_names(const char* archive_name);

/* MODs as one composite model; part 0 is the host the others attach to.
 * At least two parts. */
dmc_session* dmc_compose(const dmc_session* const* parts, const char* const* names, size_t count);

/* A PTX's textures onto the model (part < 0) or onto one composite part.
 * On false, dmc_info_text tells why. */
bool dmc_attach_ptx(dmc_session* session, int part, const char* name, const uint8_t* bytes, size_t size);

/* ---- Composite parts ----------------------------------------------------- */

typedef struct dmc_part {
    const char* name;
    const char* state;               /* the core's one-line description */
    size_t node_count;
    const char* const* node_names;
    int default_attachment;          /* the joint it hangs from by default; -1 none */
} dmc_part;

typedef struct dmc_parts dmc_parts;

/* The composite's parts now (a snapshot: build it again after a change). */
dmc_parts* dmc_parts_list(const dmc_session* session);
size_t dmc_parts_count(const dmc_parts* parts);
const dmc_part* dmc_parts_at(const dmc_parts* parts, size_t index);
void dmc_parts_free(dmc_parts* parts);

/* Hangs part `child` from joint `joint` of part `host`, or puts it back in
 * its source coordinates. Returns the core's status name ("applied",
 * "reset", or why not), freed with dmc_string_free. */
char* dmc_part_attach(dmc_session* session, size_t host, size_t child, uint32_t joint);
char* dmc_part_reset(dmc_session* session, size_t child);

/* ---- Children's pictures, UV maps, the Info picture --------------------- */

/* Child `index`'s thumbnail: its size (0 x 0 when it has none), then RGBA8. */
bool dmc_child_preview_size(const dmc_session* session, size_t index, int* width, int* height);
bool dmc_child_preview(const dmc_session* session, size_t index, uint8_t* rgba, size_t capacity);

/* The model's UV maps as a session of their own (one child per map). */
dmc_session* dmc_open_uv_gallery(const dmc_session* session);

/* A session with two pictures (a model's texture and its Info picture)
 * shows the Info one while it is set; dmc_image_preview follows. */
bool dmc_has_dual_preview(const dmc_session* session);
bool dmc_info_preview_active(const dmc_session* session);
bool dmc_set_info_preview(dmc_session* session, bool info);

/* The file a session was built from, when it is one the user may save (an
 * assembled or re-encoded file): its name and bytes, borrowed until the
 * session changes or closes. False when there is nothing to save. */
bool dmc_source(const dmc_session* session, const char** name, const uint8_t** bytes, size_t* size);

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
 * Each side is DMC_RENDER_MIN_SIDE..DMC_RENDER_MAX_SIDE pixels; a shell
 * scales a smaller view up from the smallest frame.
 * Returns 0 on failure, 1 drawn, 2 drawn but the motion could not be posed. */
#define DMC_RENDER_MIN_SIDE 64
#define DMC_RENDER_MAX_SIDE 8192
int dmc_render(dmc_session* session, int width, int height, const dmc_view* view, uint8_t* rgba,
               size_t capacity);

/* Bit 4 of dmc_render's result: the frame went straight to the window the core
 * draws into (dmc_gpu_attach, later); nothing was copied into `rgba`. */
#define DMC_RENDER_SHOWN 4

/* ---- Group 2: picking, holding, camera, fly, export, capture ------------- *
 * Every decision is the core's; a shell sends the touch, the stick or the
 * sensor reading and shows what comes back. Calls without a session work on
 * the core's camera state (camera_control), which has its own lock. */

/* What lies under point (x, y) of a width x height view. */
typedef enum dmc_pick_kind {
    DMC_PICK_NONE = 0,
    DMC_PICK_MODEL = 1,
    DMC_PICK_ROOM = 2,
    DMC_PICK_PLACED = 3          /* `place` was set and the room's actor now stands there */
} dmc_pick_kind;

typedef struct dmc_pick_result {
    int kind;                    /* dmc_pick_kind */
    int joint;                   /* the model's joint under the point, -1 none */
    char* joint_name;            /* "" when none */
    int marker;                  /* a stage configuration marker held, -1 none */
    char* marker_title;
    float room_point[3];         /* where the ray met the room (kind ROOM / PLACED) */
} dmc_pick_result;

/* With `place`, a point on the room's floor (and no model in front) moves the
 * room's actor there. Free the strings with dmc_pick_result_free. */
dmc_pick_result dmc_pick(dmc_session* session, int width, int height, const dmc_view* view, float x, float y,
                         bool place);
void dmc_pick_result_free(dmc_pick_result* result);

/* What the core tells a shell after an action: show `message`, then take one
 * platform step. Free the strings with dmc_outcome_free. */
typedef enum dmc_shell_step {
    DMC_STEP_NONE = 0,
    DMC_STEP_PICK_STAGE_CONFIG = 1,
    DMC_STEP_SHOW_MARKER_CARD = 2,
    DMC_STEP_START_SCRUB = 3,
    DMC_STEP_OPEN_SESSION = 4,
    DMC_STEP_LEAVE_FLY = 5,
    DMC_STEP_PICK_CUTSCENE_SOUND = 6,
    DMC_STEP_RESET_VIEW = 7
} dmc_shell_step;

typedef struct dmc_outcome {
    bool ok;
    int step;                    /* dmc_shell_step */
    char* message;               /* the core's text; "" when there is nothing to say */
    int marker;                  /* SHOW_MARKER_CARD: which marker, else -1 */
    char* title;                 /* OPEN_SESSION: the opened session's title */
} dmc_outcome;

void dmc_outcome_free(dmc_outcome* outcome);

/* The view's long press at (x, y): a joint's card, a marker's card, or a
 * scrub of the motion under the finger (`joints`, `scrub`: what the shell's
 * view offers right now). */
dmc_outcome dmc_hold(const dmc_session* session, int width, int height, const dmc_view* view, float x, float y,
                     bool joints, bool scrub);

/* The framed distance and how far the camera may dolly, model units. */
bool dmc_camera_metrics(const dmc_session* session, float* distance, float* dolly_limit);

/* Camera mode: the viewfinder is on (the photo camera's view). */
bool dmc_camera_mode(void);

/* Touch gestures (camera_control). A gesture is claimed when it starts; only
 * the claimed one moves the camera until dmc_end_touch. */
typedef enum dmc_gesture {
    DMC_GESTURE_NONE = 0,
    DMC_GESTURE_ZOOM = 1,
    DMC_GESTURE_DOLLY = 2,
    DMC_GESTURE_TURN = 3
} dmc_gesture;

bool dmc_claim_gesture(int gesture);
void dmc_end_touch(void);
void dmc_camera_reset(void);
/* A pinch's zoom `next` after `touch_zoom`; `settling` while the fingers lift.
 * False when the core keeps the zoom where it is. */
bool dmc_zoom_step(float touch_zoom, float next, bool settling);
/* Levels the camera (no roll); turns the roll by `radians` and returns the roll now. */
void dmc_camera_level(void);
float dmc_camera_roll(float radians);
/* The camera keeps the screen's orientation (the shell then locks rotation). */
bool dmc_camera_holds_orientation(void);

/* The gyroscope: the shell reads the sensor, the core turns the camera. */
bool dmc_gyro_active(void);
void dmc_gyro_reset(void);
/* The device's attitude now; out[0], out[1]: the yaw and pitch to show. */
void dmc_gyro_turn(float heading, float elevation, float roll, float out[2]);

/* Fly mode: the eye moves with two on-screen sticks. */
bool dmc_fly_enter(const dmc_session* session, int width, int height, const dmc_view* view);

typedef struct dmc_fly_step {
    float yaw;
    float pitch;
    float boost;                 /* the move stick's speed factor (outer ring) */
    int zone;                    /* 0 inside the dead band, then 1.. per zone reached */
} dmc_fly_step;

/* One tick of `dt` seconds: each stick held or not, its finger offset in
 * pixels from the stick's base on a screen of `density`. */
dmc_fly_step dmc_fly_tick(const dmc_session* session, float yaw, float pitch, bool move_held, float move_dx,
                          float move_dy, bool look_held, float look_dx, float look_dy, float density, float dt);
void dmc_fly_leave(const dmc_session* session, float yaw, float pitch);
/* Where the stick's knob is drawn for a finger offset; out[0], out[1] in pixels. */
void dmc_stick_knob(float dx, float dy, float density, float out[2]);
/* Walks the fly eye (model units) along the ground; false when not flying. */
bool dmc_walk(float yaw, float forward, float right, float up);
bool dmc_walk_active(void);

/* The viewfinder over a width x height view: frame and guide lines in view
 * pixels, and the status line. Free with dmc_finder_free. */
typedef struct dmc_finder {
    bool shown;
    float frame[4];              /* x, y, width, height */
    size_t line_count;
    float* lines;                /* 4 floats per line: x0, y0, x1, y1 */
    char* status;
} dmc_finder;

dmc_finder dmc_viewfinder(const dmc_session* session, int width, int height, float zoom, bool recording,
                          int recorded_seconds);
void dmc_finder_free(dmc_finder* finder);

/* An RGBA8 picture made by the core (rows top down). Free with dmc_image_free. */
typedef struct dmc_image {
    int width;
    int height;
    uint8_t* rgba;               /* width * height * 4 bytes */
} dmc_image;

void dmc_image_free(dmc_image* image);

/* PNG export of the view: its size, then the picture (NULL when the session
 * has nothing to export). The shell encodes and saves it. */
bool dmc_export_image_size(const dmc_session* session, int* width, int* height);
dmc_image* dmc_render_export(const dmc_session* session);

/* The photo camera's picture (`video` false) or video frame (true), full
 * size, for a view of view_width x view_height. view->motion_frame and
 * `motion_fps` say how the scene moves while the shutter is open. */
dmc_image* dmc_render_capture(dmc_session* session, bool video, int view_width, int view_height,
                              const dmc_view* view, float motion_fps);

/* Draws the view for `seconds` (1..120) and returns the core's report; with a
 * finite `motion_start` the motion plays from there, one game frame per drawn
 * frame. `settings` is the shell's line of active settings for the report. */
char* dmc_benchmark(dmc_session* session, int width, int height, const dmc_view* view, float motion_start,
                    float seconds, const char* settings);

/* ---- The view's touch math (#45): every shell moves the camera alike ---- *
 * A shell reports what the fingers or the pointer did; the core changes the
 * view's yaw, pitch, zoom, pan and room yaw. No session, no lock. Functions
 * returning bool say whether the view changed (draw a new frame then). */

/* A model's start view: a front three-quarter view, zoom 1, no pan. Leaves
 * render_flags, follow, dolly and motion_frame as they are. */
void dmc_view_reset(dmc_view* view);
/* One finger or the pointer moved by dx, dy pixels: grab-and-turn. */
bool dmc_view_drag(dmc_view* view, float dx, float dy);
/* Two fingers moved together over a width x height view: the content follows. */
bool dmc_view_pan(dmc_view* view, float dx, float dy, int width, int height);
/* The signed turn between two finger angles (radians), the short way round. */
float dmc_angle_delta(float from, float to);
/* Two fingers turned: with the room drawn the model turns in it, else the view. */
bool dmc_view_twist(dmc_view* view, float radians, bool room_shown);
/* A pinch by `scale` since the touch began at `touch_zoom`: the new zoom,
 * within DMC_ZOOM_MIN..DMC_ZOOM_MAX. */
#define DMC_ZOOM_MIN 0.15f
#define DMC_ZOOM_MAX 8.0f
float dmc_pinch_zoom(float touch_zoom, float scale);

/* Inertia after a flick: velocity in pixels per second, density in pixels
 * per dp (1 for a desktop pointer). */
typedef struct dmc_spin {
    float yaw_rate;
    float pitch_rate;
} dmc_spin;

/* True when the flick was fast enough to spin the view. */
bool dmc_fling(float velocity_x, float velocity_y, float density, dmc_spin* spin);
/* Advances the spin by dt seconds; false once it has stopped. */
bool dmc_spin_step(dmc_view* view, dmc_spin* spin, float dt);

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
