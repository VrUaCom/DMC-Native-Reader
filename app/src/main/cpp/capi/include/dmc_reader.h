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
 * core's own state, not a session's, and have their own locks. Opening a
 * file (dmc_open, dmc_open_source) takes no session lock: it reads only the
 * bytes and makes a new session, so a large archive opens while other
 * sessions keep drawing.
 *
 * Ownership: a session from dmc_open, dmc_open_description, dmc_open_source, dmc_open_child,
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
/* The file as its description, with no format module run: for a file the
 * reader closed on the last time it opened it (the JNI describe_only open). */
dmc_open_result dmc_open_description(const char* name, const uint8_t* bytes, size_t size);
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
/* The archives a character loads with it (the shell adds them from the same
 * folder): one "kind\tfile\tlabel\tstart" line each; kind weapon, motions
 * or dt (a devil-trigger body that replaces the body), start 1 when it is
 * added as the character opens. Empty for other archives. */
dmc_strings* dmc_character_kit(const char* archive_name);

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

/* ---- Group 3: animations (motion_player) --------------------------------- *
 * Each load returns the core's one-line report (dmc_string_free). */

/* A motion file the user picked (MOT, a motion PAC) onto the model. */
char* dmc_load_motion(dmc_session* session, const char* name, const uint8_t* bytes, size_t size);
/* Motion `index` of the session's own library (dmc_motion_strip: library_index). */
char* dmc_load_library_motion(dmc_session* session, size_t index);
/* Motion `motion` driven by MotionScript `script` (dmc_motion_strip: the card's scripts). */
char* dmc_load_scripted_motion(dmc_session* session, size_t script, size_t motion);
/* Whether script `script` can play library motion `motion`. */
bool dmc_motion_script_can_play(const dmc_session* session, size_t script, size_t motion);
/* The slot script `script` drives, -1 none. */
int dmc_motion_script_slot(const dmc_session* session, size_t script);
/* The loaded motion's last frame and the frame its loop goes back to (60 per
 * second); false when no motion is loaded. */
bool dmc_motion_range(const dmc_session* session, float* end_frame, float* loop_start);
/* An enemy class's own events (effects it fires), by name; firing one by index. */
dmc_strings* dmc_class_events(const dmc_session* session);
bool dmc_trigger_class_event(dmc_session* session, size_t index);

/* ---- Menus and cards (shared by groups 4 and 5) -------------------------- *
 * A menu the core wrote: a title, a message and items with stable ids the
 * shell sends back. A snapshot, valid until freed. */
typedef struct dmc_menu_item {
    int id;
    const char* label;
    const char* icon;            /* a glyph for square shortcuts; "" for rows */
} dmc_menu_item;

typedef struct dmc_menu dmc_menu;
const char* dmc_menu_title(const dmc_menu* menu);
const char* dmc_menu_message(const dmc_menu* menu);
size_t dmc_menu_count(const dmc_menu* menu);
const dmc_menu_item* dmc_menu_at(const dmc_menu* menu, size_t index);  /* NULL past the end */
/* Item ids the menu marks: ready (the archive is loaded) / warn (another stage's). */
bool dmc_menu_ready(const dmc_menu* menu, int id);
bool dmc_menu_warn(const dmc_menu* menu, int id);
void dmc_menu_free(dmc_menu* menu);

/* ---- Group 4: stages, the base room, collision ---------------------------- *
 * The base room, its effects and the stage configurations are the core's
 * own state, not a session's (stage_room / stage_markers, each with its own
 * lock): those calls take no session. `slot` is 0 for the base room's set,
 * 1 for the opened stage's (stage_slot.h). */

/* The base room every model stands in: a stNNN file read from `bytes`.
 * Returns the core's one-line detail, NULL when it is not a room. */
char* dmc_room_load(const char* name, const uint8_t* bytes, size_t size);
void dmc_room_clear(void);
void dmc_room_set_broken(bool broken);
size_t dmc_room_spot_count(void);
size_t dmc_room_collision_source_count(void);
/* The room draws animated set objects for this session's view. */
bool dmc_room_animated(const dmc_session* session);

/* The base room's spawn points (its configuration's), by title. */
dmc_strings* dmc_base_spawn_points(void);
/* Stands the model on spawn point `index` (-1: the room's own floor spot). */
bool dmc_place_at_spawn(int index);

typedef struct dmc_base_spot {
    bool spawn;                  /* a configuration's spawn point, not a floor spot */
    int index;                   /* -1 when there is none */
    int count;
    char* title;
} dmc_base_spot;
/* "Next spot": the next spawn point, else the next floor spot. */
dmc_base_spot dmc_next_base_spot(void);
void dmc_base_spot_free(dmc_base_spot* spot);

/* A new base room was loaded: which of the current effect bank and
 * configuration stay, and which files of its own stage to look for. */
typedef struct dmc_room_change {
    bool keep_effects;
    bool keep_config;
    char* find_effects;
    char* find_config;
} dmc_room_change;
dmc_room_change dmc_after_new_room(const char* room, const char* effects, const char* config);
void dmc_room_change_free(dmc_room_change* change);
/* What a room preset square shows under its number ("st001"; "" none). */
char* dmc_preset_label(const char* room);
/* The base room's Stage Ops (room, effect bank, configuration, sounds). */
dmc_menu* dmc_base_room_menu(bool room_loaded);

/* A stage's effect bank onto slot `slot`; the core's detail, NULL when refused. */
char* dmc_room_effects_load(const char* name, const uint8_t* bytes, size_t size, int slot);
void dmc_room_effects_clear(int slot);
/* The game's common effect bank (static_effect.pac): one for the reader,
 * listed after each file's own effects and its stage's; a room's bank does
 * not replace it. The core's detail, NULL when refused. */
char* dmc_common_effects_load(const char* name, const uint8_t* bytes, size_t size);
void dmc_common_effects_clear(void);

/* A stNNNcfg.pac (or one member) as slot `slot`'s configuration. */
dmc_outcome dmc_stage_config_load(const char* name, const uint8_t* bytes, size_t size, int slot);
void dmc_stage_config_clear(int slot);
/* The effect bank and configuration a stage's files are named (false: not a stage name). */
bool dmc_stage_archives(const char* stage_name, char** effects, char** config);
/* Which slot a session draws into (0 base, 1 scene). */
int dmc_stage_slot(const dmc_session* session);
/* A stage session's start view: yaw, pitch, zoom. False for other sessions. */
bool dmc_stage_start_view(dmc_session* session, float out[3]);
/* "st006" for a stage session; "" otherwise. */
char* dmc_stage_stem(const dmc_session* session);

/* Collision of a stage (HITS) or a model's attacks (hitboxes). */
size_t dmc_stage_collision_source_count(const dmc_session* session);

typedef struct dmc_collision_kind {
    uint32_t flags;
    size_t count, floors, walls, ceilings;
    uint32_t rgb;                /* its drawing colour, 0xRRGGBB */
} dmc_collision_kind;
/* The kinds of HITS the session shows; returns how many there are, writes
 * at most `capacity`. */
size_t dmc_collision_kinds(const dmc_session* session, dmc_collision_kind* out, size_t capacity);
/* The attack ids with hitboxes; returns how many there are, writes at most `capacity`. */
size_t dmc_collision_attack_ids(const dmc_session* session, int* out, size_t capacity);
/* Shows only `attack`'s hitboxes (-1: all); the core's description, "" when refused. */
char* dmc_select_collision_attack(dmc_session* session, int attack);

typedef struct dmc_report_group {
    int64_t rgb;                 /* 0xRRGGBB, -1 when the group has no colour */
    const char* heading;
    const char* lines;           /* the group's lines joined by '\n' */
} dmc_report_group;
typedef struct dmc_report dmc_report;
/* The stage's collision report, or the attack one (`attack`). */
dmc_report* dmc_collision_report(const dmc_session* session, bool attack);
const char* dmc_report_title(const dmc_report* report);
const char* dmc_report_summary(const dmc_report* report);
size_t dmc_report_group_count(const dmc_report* report);
const dmc_report_group* dmc_report_group_at(const dmc_report* report, size_t index);
void dmc_report_free(dmc_report* report);

/* A configuration marker's card: title, name/value rows and its buttons. */
typedef struct dmc_card dmc_card;
dmc_card* dmc_marker_card(int marker);   /* NULL when the marker has no card */
const char* dmc_card_title(const dmc_card* card);
size_t dmc_card_row_count(const dmc_card* card);
bool dmc_card_row(const dmc_card* card, size_t index, const char** name, const char** value);
const dmc_menu* dmc_card_actions(const dmc_card* card);  /* owned by the card */
void dmc_card_free(dmc_card* card);
/* A card button; "Open file" hands the file back in *opened (the caller's,
 * closed with dmc_close) with DMC_STEP_OPEN_SESSION. */
dmc_outcome dmc_run_card_action(int marker, int id, dmc_session** opened);

/* ---- Group 5: the companion menu, effects, sounds -------------------------- */

/* What only the shell knows when it asks for the menu. */
typedef struct dmc_shell_facts {
    bool root_scene;             /* nothing navigated into */
    bool assembled_pac_source;   /* the assembled PAC's document is kept */
    bool stage_source;           /* the stage archive's document is kept */
    bool room_loaded;
    bool room_shown;
    bool recording;              /* a video of the view is being recorded */
} dmc_shell_facts;

typedef enum dmc_companion_part {
    DMC_COMPANION_SHORTCUTS = 0, /* squares with a glyph */
    DMC_COMPANION_ROWS = 1,
    DMC_COMPANION_MORE = 2,      /* under a folded "Other actions" row */
    DMC_COMPANION_STAGE_OPS = 3  /* the four archives of a stage (dmc_menu_ready / _warn) */
} dmc_companion_part;

typedef struct dmc_companion dmc_companion;
/* The menu of what can be done with the session now, decided by the core. */
dmc_companion* dmc_companion_menu(const dmc_session* session, const dmc_shell_facts* facts);
const dmc_menu* dmc_companion_get(const dmc_companion* menu, int part);  /* owned by the companion */
/* A shortcut drawn dimmed (the room while hidden). */
bool dmc_companion_dimmed(const dmc_companion* menu, int id);
void dmc_companion_free(dmc_companion* menu);
/* An item chosen; a file it opens comes back in *opened (the caller's). */
dmc_outcome dmc_run_companion_action(dmc_session* session, int id, dmc_session** opened);

/* Every effect the view can play, and playing one (or stopping, id -1). */
dmc_menu* dmc_effect_menu(const dmc_session* session);
dmc_outcome dmc_play_effect(dmc_session* session, int id);

/* The session's sounds (a VAG's chunks) and one of them as 16-bit PCM. The
 * shell plays the samples; `message` is the core's line when there are none. */
dmc_menu* dmc_sound_menu(const dmc_session* session);

typedef struct dmc_pcm {
    uint32_t sample_rate;
    size_t sample_count;
    int16_t* samples;            /* mono */
    char* message;
} dmc_pcm;
dmc_pcm dmc_sound_pcm(const dmc_session* session, int index);
void dmc_pcm_free(dmc_pcm* pcm);

/* ---- Group 6: cutscenes (demo) ------------------------------------------- */

/* The camera cuts with the cutscene's director (the shell may vibrate on cuts). */
bool dmc_camera_follows_cuts(const dmc_session* session);
/* The cutscene's mixed sound the shell streams: rate and channels (false: none). */
bool dmc_cutscene_audio_format(const dmc_session* session, uint32_t* sample_rate, uint32_t* channels);
/* The sample frame that plays at motion frame `frame`. */
int64_t dmc_cutscene_audio_position(const dmc_session* session, float frame);
/* Reads up to `frames` sample frames from `position` into `out` (interleaved,
 * `capacity` int16 values); returns the values written. */
size_t dmc_cutscene_audio_read(const dmc_session* session, int64_t position, size_t frames, int16_t* out,
                               size_t capacity);

/* The subtitle shown now and how it is drawn (fractions of the view). */
typedef struct dmc_subtitle {
    char* text;                  /* lines joined by '\n' */
    float centre_x, top, size, alpha;
    uint8_t front[3], back[3];   /* text and outline colours */
} dmc_subtitle;
bool dmc_cutscene_subtitle(const dmc_session* session, dmc_subtitle* out);
void dmc_subtitle_free(dmc_subtitle* subtitle);
/* Takes the director's camera into the user's (fly eye there): yaw, pitch, zoom. */
bool dmc_cutscene_take_camera(dmc_session* session, float out[3]);
/* Vibration strength at `frame` (0 none). */
int dmc_cutscene_vibration(const dmc_session* session, float frame);
/* A cutscene's .ogg chosen by the user. */
dmc_outcome dmc_cutscene_sound_load(dmc_session* session, const char* name, const uint8_t* bytes, size_t size);

/* ---- Group 7: video of the view ------------------------------------------ */

/* A new recording starts (the motion blur forgets earlier frames). */
void dmc_video_begin(void);
/* The size a video frame of a view_width x view_height view is recorded at. */
bool dmc_video_frame_size(int view_width, int view_height, int* width, int* height);
/* One video frame into `rgba` (dmc_video_frame_size). Returns 0 on failure,
 * 1 drawn, 2 drawn but the motion could not be posed. */
int dmc_video_frame(dmc_session* session, int view_width, int view_height, const dmc_view* view, uint8_t* rgba,
                    size_t capacity);

/* ---- Group 8: the photo camera ------------------------------------------ *
 * Its settings and dials are the core's (spider/camera.h); the shell stores
 * the chosen option of each setting and gives them back in order. */

typedef struct dmc_camera_setting {
    int key;                     /* the setting's key (dials and follow-ups name it) */
    const char* id;              /* stable name the shell stores the choice under */
    const char* section;
    const char* label;
    const char* hint;
    size_t option_count;
    const char* const* options;
    int start;                   /* the starting option */
} dmc_camera_setting;

typedef struct dmc_camera_settings dmc_camera_settings;
dmc_camera_settings* dmc_camera_settings_list(void);
size_t dmc_camera_settings_count(const dmc_camera_settings* list);
const dmc_camera_setting* dmc_camera_settings_at(const dmc_camera_settings* list, size_t index);
void dmc_camera_settings_free(dmc_camera_settings* list);
/* The chosen option of each setting, in list order. */
void dmc_camera_set_choices(const int* choices, size_t count);

/* The dials of the camera bar: the setting each turns, its short name and value. */
typedef struct dmc_camera_dial {
    int key;
    const char* title;
    const char* value;           /* as a camera shows it: "ISO 400", "1/60" */
    size_t with_count;
    const int* with;             /* settings shown together with it */
} dmc_camera_dial;
typedef struct dmc_camera_dials dmc_camera_dials;
dmc_camera_dials* dmc_camera_dials_list(void);
size_t dmc_camera_dials_count(const dmc_camera_dials* list);
const dmc_camera_dial* dmc_camera_dials_at(const dmc_camera_dials* list, size_t index);
void dmc_camera_dials_free(dmc_camera_dials* list);

/* What changes with setting `key` set to `option` (a camera's M dial, ...). */
typedef struct dmc_camera_follow_up {
    char* message;
    size_t count;
    int* keys;
    int* options;
} dmc_camera_follow_up;
bool dmc_camera_follow_up_of(int key, int option, dmc_camera_follow_up* out);
void dmc_camera_follow_up_free(dmc_camera_follow_up* follow_up);

/* How a picture (`video` false) or a video of a view is taken. */
typedef struct dmc_camera_plan {
    int width, height;
    int format;                  /* picture: PNG 0, JPEG 1, WebP 2, DNG 3, DNG+JPEG 4; video: AVC 0, HEVC 1 */
    int quality;                 /* picture 0..100; video bits per second */
    int fps, keyframe_ms, limit_seconds, timer_seconds, burst;
    bool subtitles;
} dmc_camera_plan;
dmc_camera_plan dmc_camera_plan_of(bool video, int view_width, int view_height);

/* Focus: at a point of the view, the AF lock, the focus ring; each returns
 * the core's line ("" when nothing is in focus there). */
char* dmc_camera_focus_at(const dmc_session* session, int width, int height, const dmc_view* view, float x, float y);
char* dmc_camera_focus_lock(void);
char* dmc_camera_focus_ring(float steps);

/* The circular polarizer: whether two fingers turn it now, turning it, setting it. */
bool dmc_camera_polarizer_turnable(void);
float dmc_camera_polarizer_turn(float radians);
void dmc_camera_set_polarizer(float radians);

/* A RAW (DNG) picture written to the open file `fd`; with `developed`, also
 * the developed picture (RAW + JPEG), the caller's (dmc_image_free). */
bool dmc_capture_dng(dmc_session* session, int view_width, int view_height, const dmc_view* view, float motion_fps,
                     int fd, dmc_image** developed);

/* ---- Group 9: diagnostics, crash notes, texture re-encoding --------------- */

/* The commit the core was built from ("unknown" outside git). */
char* dmc_build_identity(void);
/* The settings line a benchmark report carries, from the stored values. */
char* dmc_benchmark_settings(const char* const* keys, const float* values, size_t count);
/* The session's diagnostics text, and one inspection topic (1 UV, 2 meshes, 3 hierarchy). */
char* dmc_diagnostics(const dmc_session* session);
char* dmc_inspect(const dmc_session* session, int topic);
/* The native crash note: where it is written, and what the app is doing now
 * (the note's "Doing:" line). Async-signal-safe once installed. */
void dmc_crash_install(const char* path);
void dmc_crash_context(const char* text);
/* Ends the process with SIGABRT, to try the crash note (a debug menu's). */
void dmc_test_crash(void);
/* The texture formats a model's textures can be re-encoded to (names the core
 * reads, labels the user reads; same order). */
dmc_strings* dmc_texture_format_names(void);
dmc_strings* dmc_texture_format_labels(void);
/* The model with its textures re-encoded to `format` (a name above); a new
 * session, the caller's. *detail (when given) receives the core's report. */
dmc_session* dmc_reencode_textures(const dmc_session* session, const char* format, bool force_dx10, char** detail);

/* The C API's version: a shell checks it was built against the same core. */
#define DMC_API_VERSION 2
int dmc_api_version(void);

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
