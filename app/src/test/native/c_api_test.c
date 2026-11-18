/* The C API is plain C (the Swift shells import it): this test is C. */
#include "dmc_reader.h"

#include <assert.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

static void put_u32(uint8_t* bytes, size_t at, uint32_t value) {
    bytes[at] = (uint8_t)value;
    bytes[at + 1] = (uint8_t)(value >> 8);
    bytes[at + 2] = (uint8_t)(value >> 16);
    bytes[at + 3] = (uint8_t)(value >> 24);
}

static void put_u16(uint8_t* bytes, size_t at, uint16_t value) {
    bytes[at] = (uint8_t)value;
    bytes[at + 1] = (uint8_t)(value >> 8);
}

static void put_f32(uint8_t* bytes, size_t at, float value) {
    uint32_t raw;
    memcpy(&raw, &value, sizeof raw);
    put_u32(bytes, at, raw);
}

static void put_u64(uint8_t* bytes, size_t at, uint64_t value) {
    put_u32(bytes, at, (uint32_t)value);
    put_u32(bytes, at + 4, (uint32_t)(value >> 32));
}

/* One textureless triangle in a MOD (core_model_pipeline_test's make_mod). */
static size_t make_mod(uint8_t* bytes) {
    memset(bytes, 0, 0x240);
    memcpy(bytes, "MOD ", 4);
    put_f32(bytes, 0x04, 1.01f);
    bytes[0x10] = 1;
    bytes[0x11] = 1;
    put_u64(bytes, 0x20, 0x200);
    bytes[0x40] = 1;
    put_u16(bytes, 0x42, 3);
    put_u64(bytes, 0x48, 0x80);
    put_u16(bytes, 0x80, 3);
    put_u16(bytes, 0x82, 5);
    put_u16(bytes, 0x84, 1);
    put_u16(bytes, 0x86, 2);
    put_u16(bytes, 0x88, 3);
    put_u16(bytes, 0x8A, 4);
    put_u64(bytes, 0x90, 0xD0);
    put_u64(bytes, 0x98, 0x100);
    put_u64(bytes, 0xA0, 0x130);
    put_u64(bytes, 0xA8, 0x140);
    put_u64(bytes, 0xB0, 0x150);
    put_u64(bytes, 0xC0, 0xE0);
    put_f32(bytes, 0xDC, 1.0f);
    put_f32(bytes, 0xEC, 1.0f);
    for (size_t i = 0; i < 3; ++i) put_f32(bytes, 0x100 + i * 12 + 8, 1.0f);
    put_u16(bytes, 0x134, 4096);
    put_u16(bytes, 0x13A, 4096);
    put_u16(bytes, 0x150, 0x001F);
    put_u16(bytes, 0x152, 0x001F);
    put_u16(bytes, 0x154, 0x001F);
    put_u32(bytes, 0x200, 0x10);
    put_u32(bytes, 0x204, 0x20);
    put_u32(bytes, 0x208, 0x30);
    bytes[0x210] = 0xFF;
    return 0x240;
}

/* A 4x4 DXT1 texture, one mip: red and green ends, all texels red. */
static size_t make_dds(uint8_t* bytes) {
    memset(bytes, 0, 136);
    memcpy(bytes, "DDS ", 4);
    put_u32(bytes, 4, 124);
    put_u32(bytes, 8, 0x000A1007u);
    put_u32(bytes, 12, 4);
    put_u32(bytes, 16, 4);
    put_u32(bytes, 20, 8);
    put_u32(bytes, 28, 1);
    put_u32(bytes, 76, 32);
    put_u32(bytes, 80, 4);
    memcpy(bytes + 84, "DXT1", 4);
    put_u32(bytes, 108, 0x00001000u);
    bytes[128] = 0x00; bytes[129] = 0xF8;  /* colour 0: red */
    bytes[130] = 0xE0; bytes[131] = 0x07;  /* colour 1: green */
    return 136;
}

int main(void) {
    /* Opening is the core's decision; an empty file is rejected with the reason. */
    dmc_open_result rejected = dmc_open("empty.bin", NULL, 0);
    assert(rejected.session == NULL && rejected.opened_as == DMC_OPENED_REJECTED);
    assert(rejected.title != NULL && strstr(rejected.title, "Rejected: ") != NULL);
    dmc_string_free(rejected.title);

    uint8_t dds[136];
    const size_t size = make_dds(dds);
    dmc_open_result opened = dmc_open("red.dds", dds, size);
    assert(opened.session != NULL && opened.opened_as == DMC_OPENED_PLAIN && !opened.room_effects);
    assert(strcmp(opened.title, "red.dds") == 0);
    dmc_string_free(opened.title);

    /* Info and health are written by the core. */
    const dmc_staged_file staged[1] = {{"Motion", "pl000_mot.pac"}};
    char* info = dmc_info_text(opened.session, "red.dds", 1, "pl000.pac", staged, 1);
    assert(info != NULL && strncmp(info, "red.dds\n\nSTRUCTURE\n", 19) == 0);
    assert(strstr(info, "Back returns to: pl000.pac") != NULL);
    assert(strstr(info, "- Motion: pl000_mot.pac") != NULL);
    dmc_string_free(info);
    char* report = NULL;
    const int level = dmc_file_health(opened.session, "red.dds", &report);
    assert(level >= 0 && level <= 2 && report != NULL);
    dmc_string_free(report);
    char* welcome = dmc_welcome_text("1.0", "test");
    assert(welcome != NULL && strncmp(welcome, "DMC Native Reader 1.0", 21) == 0);
    dmc_string_free(welcome);

    /* The texture draws on the CPU: a frame comes back, the centre texel red. */
    dmc_settings* schema = dmc_settings_schema(false);
    assert(schema != NULL && dmc_settings_count(schema) > 10);
    const char* keys[64];
    float values[64];
    size_t stored = 0;
    for (size_t index = 0; index < dmc_settings_count(schema); ++index) {
        const dmc_setting_row* row = dmc_settings_row(schema, index);
        assert(row != NULL && row->key != NULL && row->section != NULL);
        if (row->control == DMC_SETTING_SLOT) continue;
        assert(row->choice_count > 0 && row->values[dmc_settings_selected(row, row->fallback)] == row->fallback);
        keys[stored] = row->key;
        values[stored] = strcmp(row->key, "set.gpu") == 0 ? 0.0f : row->fallback;  /* CPU here */
        ++stored;
    }
    assert(dmc_settings_row(schema, dmc_settings_count(schema)) == NULL);
    const dmc_applied_settings applied = dmc_apply_settings(keys, values, stored);
    assert(applied.max_side == 720 && applied.frame_ms == 33 && applied.speed == 1.0f);

    /* A texture is shown as its own picture: 4 x 4, red. */
    int pw = 0, ph = 0;
    assert(dmc_image_preview_size(opened.session, &pw, &ph) && pw == 4 && ph == 4);
    uint8_t picture[4 * 4 * 4];
    assert(!dmc_image_preview(opened.session, picture, 8));
    assert(dmc_image_preview(opened.session, picture, sizeof picture));
    assert(picture[0] > 200 && picture[1] < 60 && picture[2] < 60 && picture[3] == 255);

    /* A model draws through the view: a frame comes back with something on it. */
    static uint8_t mod[0x240];
    const size_t mod_size = make_mod(mod);
    dmc_open_result model = dmc_open("tri.mod", mod, mod_size);
    assert(model.session != NULL && model.opened_as == DMC_OPENED_PLAIN);
    dmc_string_free(model.title);
    assert(!dmc_image_preview_size(model.session, &pw, &ph) && pw == 0 && ph == 0);
    dmc_view view;
    memset(&view, 0, sizeof view);
    view.zoom = 1.0f;
    view.render_flags = applied.render_flags;
    view.motion_frame = NAN;
    enum { W = 64, H = 64 };
    uint8_t* pixels = calloc((size_t)W * H * 4, 1);
    assert(pixels != NULL);
    assert(dmc_render(model.session, W, H, &view, pixels, 16) == 0);  /* too small a buffer */
    assert(dmc_render(model.session, DMC_RENDER_MIN_SIDE - 1, H, &view, pixels, (size_t)W * H * 4) == 0);
    assert(dmc_render(model.session, W, H, &view, pixels, (size_t)W * H * 4) == 1);
    size_t differing = 0;
    for (size_t i = 0; i < (size_t)W * H; ++i) {
        if (memcmp(pixels + i * 4, pixels, 4) != 0) ++differing;
    }
    assert(differing > 0);  /* not one flat background */
    assert(dmc_render(opened.session, W, H, &view, pixels, (size_t)W * H * 4) == 0);  /* a texture: no 3D view */

    /* Group 1: two MODs opened as sources compose into one model of two parts. */
    dmc_session* base = dmc_open_source("base.mod", mod, mod_size);
    dmc_session* extra = dmc_open_source("extra.mod", mod, mod_size);
    assert(base != NULL && extra != NULL && dmc_open_source("empty.bin", NULL, 0) == NULL);
    const dmc_session* sources[2] = {base, extra};
    const char* source_names[2] = {"base.mod", "extra.mod"};
    assert(dmc_compose(sources, source_names, 1) == NULL);  /* one part is not a composition */
    dmc_session* composite = dmc_compose(sources, source_names, 2);
    assert(composite != NULL);
    dmc_close(extra);  /* the composite owns what it needs */
    dmc_parts* parts = dmc_parts_list(composite);
    assert(parts != NULL && dmc_parts_count(parts) == 2 && dmc_parts_at(parts, 2) == NULL);
    const dmc_part* host_part = dmc_parts_at(parts, 0);
    assert(host_part->name != NULL && host_part->state != NULL && strlen(host_part->state) > 0);
    assert(host_part->node_count == 0 || host_part->node_names[0] != NULL);
    char* same = dmc_part_attach(composite, 1, 1, 0);
    assert(same != NULL && strcmp(same, "same-part") == 0);
    dmc_string_free(same);
    char* outside = dmc_part_attach(composite, 0, 7, 0);
    assert(outside != NULL && strcmp(outside, "invalid-part-index") == 0);
    dmc_string_free(outside);
    char* reset = dmc_part_reset(composite, 1);
    assert(reset != NULL && strcmp(reset, "reset") == 0);
    dmc_string_free(reset);
    dmc_parts_free(parts);
    uint8_t* frame = calloc((size_t)W * H * 4, 1);
    assert(frame != NULL && dmc_render(composite, W, H, &view, frame, (size_t)W * H * 4) == 1);
    free(frame);
    /* A PTX that is not one is refused; the model stays as it was. */
    const uint8_t junk[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    assert(!dmc_attach_ptx(composite, -1, "junk.ptx", junk, sizeof junk));
    assert(!dmc_attach_ptx(composite, 0, "junk.ptx", junk, sizeof junk));
    /* Nothing of its own to save; no thumbnails, no Info picture. */
    const char* source_name = "x";
    const uint8_t* source_bytes = junk;
    size_t source_size = 1;
    assert(!dmc_source(composite, &source_name, &source_bytes, &source_size));
    assert(source_name == NULL && source_bytes == NULL && source_size == 0);
    assert(!dmc_child_preview_size(composite, 0, &pw, &ph) && pw == 0 && ph == 0);
    assert(!dmc_has_dual_preview(composite) && !dmc_info_preview_active(composite));
    dmc_close(composite);
    /* A single MOD is not an archive set to assemble. */
    const dmc_session* lone[1] = {base};
    const char* lone_name[1] = {"base.mod"};
    dmc_session* assembled = dmc_assemble(lone, lone_name, 1, 0);
    dmc_close(assembled);
    assert(dmc_assemble(NULL, NULL, 0, 0) == NULL);
    dmc_close(base);

    /* em000.pac holds several enemy classes; an unknown archive holds one. */
    dmc_strings* variants = dmc_variant_names("em000.pac");
    assert(variants != NULL && dmc_strings_count(variants) > 1);
    assert(dmc_strings_at(variants, 0) != NULL && dmc_strings_at(variants, dmc_strings_count(variants)) == NULL);
    dmc_strings_free(variants);
    dmc_strings* none = dmc_variant_names("unknown.pac");
    assert(none != NULL && dmc_strings_count(none) <= 1);
    dmc_strings_free(none);
    /* Group 2: what the finger is on. A pixel of the drawn triangle picks the
     * model; a background corner picks nothing; a bad size picks nothing. */
    assert(dmc_render(model.session, W, H, &view, pixels, (size_t)W * H * 4) == 1);
    int hit_x = -1, hit_y = -1;
    for (int y = 0; y < H && hit_x < 0; ++y) {
        for (int x = 0; x < W; ++x) {
            if (memcmp(pixels + ((size_t)y * W + (size_t)x) * 4, pixels, 4) != 0) { hit_x = x; hit_y = y; break; }
        }
    }
    assert(hit_x >= 0);
    dmc_pick_result on_model = dmc_pick(model.session, W, H, &view, (float)hit_x + 0.5f, (float)hit_y + 0.5f, false);
    dmc_pick_result on_nothing = dmc_pick(model.session, W, H, &view, 0.5f, 0.5f, false);
    dmc_pick_result bad_size = dmc_pick(model.session, 1, H, &view, 0.5f, 0.5f, true);
    assert(on_nothing.kind == DMC_PICK_NONE && on_nothing.joint_name != NULL && on_nothing.marker_title != NULL);
    assert(bad_size.kind == DMC_PICK_NONE && bad_size.joint_name != NULL && bad_size.marker == -1);
    assert(on_model.kind == DMC_PICK_MODEL || on_model.kind == DMC_PICK_NONE);  /* an anti-aliased edge */
    /* Over a grid: the model is found, and only where something was drawn. */
    int model_hits = 0;
    for (int gy = 4; gy < H; gy += 4) {
        for (int gx = 4; gx < W; gx += 4) {
            dmc_pick_result g = dmc_pick(model.session, W, H, &view, (float)gx, (float)gy, false);
            if (g.kind == DMC_PICK_MODEL) {
                ++model_hits;
                assert(memcmp(pixels + ((size_t)gy * W + (size_t)gx) * 4, pixels, 4) != 0);
            }
            dmc_pick_result_free(&g);
        }
    }
    assert(model_hits > 0);
    dmc_pick_result_free(&on_model);
    dmc_pick_result_free(&on_nothing);
    dmc_pick_result_free(&bad_size);
    dmc_pick_result_free(NULL);

    /* The long press: the core's outcome, strings always there to free. */
    dmc_outcome held = dmc_hold(model.session, W, H, &view, (float)hit_x + 0.5f, (float)hit_y + 0.5f, true, false);
    assert(held.message != NULL && held.title != NULL);
    assert(held.step >= DMC_STEP_NONE && held.step <= DMC_STEP_RESET_VIEW);
    dmc_outcome_free(&held);
    dmc_outcome no_view = dmc_hold(model.session, W, H, NULL, 1.0f, 1.0f, true, true);
    assert(!no_view.ok && no_view.step == DMC_STEP_NONE && no_view.message != NULL);
    dmc_outcome_free(&no_view);

    /* Camera metrics come from the session; without one there are none. */
    float distance = -1.0f, dolly = -1.0f;
    assert(dmc_camera_metrics(model.session, &distance, &dolly) && distance > 0.0f && dolly >= 0.0f);
    assert(!dmc_camera_metrics(NULL, &distance, &dolly));

    /* Gestures: only claimed ones move the camera; ids outside the enum are refused. */
    assert(!dmc_claim_gesture(9) && !dmc_claim_gesture(-1));
    dmc_end_touch();
    dmc_camera_reset();
    float knob[2] = {1.0f, 1.0f};
    dmc_stick_knob(0.0f, 0.0f, 2.0f, knob);
    assert(knob[0] == 0.0f && knob[1] == 0.0f);
    dmc_stick_knob(0.0f, 0.0f, 2.0f, NULL);
    float turn[2] = {NAN, NAN};
    dmc_gyro_turn(0.0f, 0.0f, 0.0f, turn);
    assert(isfinite(turn[0]) && isfinite(turn[1]));
    assert(!dmc_gyro_active());

    /* Fly: enter from the framed view, tick a held move stick, leave. */
    assert(!dmc_fly_enter(model.session, W, H, NULL));
    assert(dmc_fly_enter(model.session, W, H, &view));
    {
        const dmc_fly_step still = dmc_fly_tick(model.session, 0.3f, 0.1f, false, 0, 0, false, 0, 0, 2.0f, 0.016f);
        assert(isfinite(still.yaw) && isfinite(still.pitch) && still.zone == 0);
        const dmc_fly_step moved = dmc_fly_tick(model.session, 0.3f, 0.1f, true, 0.0f, -400.0f, false, 0, 0, 2.0f, 0.016f);
        assert(isfinite(moved.yaw) && moved.zone >= 1 && moved.boost >= 1.0f);
        dmc_fly_leave(model.session, 0.3f, 0.1f);
    }
    dmc_camera_reset();

    /* The viewfinder of a model view: its status line, lines four floats each. */
    dmc_finder finder = dmc_viewfinder(model.session, W, H, 1.0f, false, 0);
    assert(finder.status != NULL && (finder.line_count == 0 || finder.lines != NULL));
    dmc_finder_free(&finder);
    assert(finder.status == NULL && finder.lines == NULL && finder.line_count == 0);

    /* Export and capture: the core's picture, or nothing; never a picture of another size. */
    int ew = 0, eh = 0;
    const bool exportable = dmc_export_image_size(model.session, &ew, &eh);
    dmc_image* exported = dmc_render_export(model.session);
    assert(!exportable && ew == 0 && eh == 0 && exported == NULL);  /* PNG export is a stage's; a model has none */
    assert(dmc_render_export(NULL) == NULL && !dmc_export_image_size(NULL, &ew, &eh));
    dmc_image* photo = dmc_render_capture(model.session, false, W, H, &view, 30.0f);
    assert(photo != NULL && photo->width > 0 && photo->height > 0 && photo->rgba != NULL);
    dmc_image_free(photo);
    assert(dmc_render_capture(model.session, false, W, H, NULL, 30.0f) == NULL);
    dmc_image_free(NULL);

    /* The benchmark draws for a second and reports in the core's words. */
    char* bench = dmc_benchmark(model.session, W, H, &view, NAN, 1.0f, "test");
    assert(bench != NULL && strlen(bench) > 10);
    dmc_string_free(bench);
    char* nothing = dmc_benchmark(NULL, W, H, &view, NAN, 1.0f, NULL);
    assert(nothing != NULL && strcmp(nothing, "Nothing to draw.") == 0);
    dmc_string_free(nothing);

    /* #45: the touch math is the core's; a shell only reports the fingers. */
    dmc_view touch = view;
    touch.render_flags = 77u;
    dmc_view_reset(&touch);
    assert(fabsf(touch.yaw - (3.14159265f - 0.65f)) < 1e-5f && fabsf(touch.pitch + 0.45f) < 1e-5f);
    assert(touch.zoom == 1.0f && touch.pan_x == 0.0f && touch.render_flags == 77u);  /* flags untouched */
    const float yaw_before = touch.yaw;
    assert(dmc_view_drag(&touch, 100.0f, 0.0f) && fabsf(touch.yaw - (yaw_before - 0.8f)) < 1e-5f);
    assert(!dmc_view_drag(&touch, 0.0f, 0.0f) && !dmc_view_drag(NULL, 1.0f, 1.0f));
    assert(dmc_view_pan(&touch, 74.0f, 0.0f, 740, 1000) && fabsf(touch.pan_x + 0.296f) < 1e-5f);
    assert(dmc_view_twist(&touch, 0.5f, true) && fabsf(touch.room_yaw + 0.5f) < 1e-5f);
    assert(fabsf(dmc_angle_delta(3.0f, -3.0f) - (6.2831853f - 6.0f)) < 1e-4f);
    assert(dmc_pinch_zoom(1.0f, 100.0f) == DMC_ZOOM_MAX && dmc_pinch_zoom(1.0f, 0.0001f) == DMC_ZOOM_MIN);
    dmc_spin spin;
    assert(!dmc_fling(10.0f, 0.0f, 1.0f, &spin) && spin.yaw_rate == 0.0f);
    assert(dmc_fling(2000.0f, 0.0f, 1.0f, &spin) && spin.yaw_rate > 0.0f);
    int spun = 0;
    while (dmc_spin_step(&touch, &spin, 0.016f)) ++spun;
    assert(spun > 10 && spin.yaw_rate == 0.0f);
    /* A frame of the touched view still draws. */
    assert(dmc_render(model.session, W, H, &touch, pixels, (size_t)W * H * 4) >= 1);

    /* Group 3: a model without motions. Loads report in the core's words and
     * never fail silently; out-of-range indices are refused. */
    float end_frame = -1.0f, loop_start = -1.0f;
    assert(!dmc_motion_range(model.session, &end_frame, &loop_start) && end_frame == -1.0f);
    char* junk_motion = dmc_load_motion(model.session, "junk.mot", junk, sizeof junk);
    assert(junk_motion != NULL && strlen(junk_motion) > 0);
    dmc_string_free(junk_motion);
    char* empty_motion = dmc_load_motion(model.session, "empty.mot", NULL, 0);
    assert(empty_motion != NULL && strcmp(empty_motion, "Motion: empty file") == 0);
    dmc_string_free(empty_motion);
    char* no_model = dmc_load_motion(NULL, "a.mot", junk, sizeof junk);
    assert(no_model != NULL && strcmp(no_model, "Motion: no model session") == 0);
    dmc_string_free(no_model);
    char* library = dmc_load_library_motion(model.session, 99);
    assert(library != NULL && strcmp(library, "Motion: invalid library index") == 0);
    dmc_string_free(library);
    char* scripted = dmc_load_scripted_motion(model.session, 0, 0);
    assert(scripted != NULL);
    dmc_string_free(scripted);
    assert(!dmc_motion_script_can_play(model.session, 0, 0) && dmc_motion_script_slot(model.session, 0) == -1);
    dmc_strings* events = dmc_class_events(model.session);
    assert(events != NULL && dmc_strings_count(events) == 0);
    dmc_strings_free(events);
    assert(!dmc_trigger_class_event(model.session, 0));
    assert(dmc_render(model.session, W, H, &view, pixels, (size_t)W * H * 4) == 1);  /* still draws */

    /* Group 4: no room and no stage loaded. The room is the core's state:
     * junk is refused, counts are zero, menus and reports still come back. */
    dmc_room_clear();
    assert(dmc_room_load("junk.pac", junk, sizeof junk) == NULL && dmc_room_load("x", NULL, 0) == NULL);
    assert(dmc_room_spot_count() == 0 && dmc_room_collision_source_count() == 0);
    assert(!dmc_room_animated(model.session) && !dmc_room_animated(NULL));
    dmc_room_set_broken(true);
    dmc_room_set_broken(false);
    dmc_strings* spawns = dmc_base_spawn_points();
    assert(spawns != NULL && dmc_strings_count(spawns) == 0);
    dmc_strings_free(spawns);
    assert(!dmc_place_at_spawn(3));
    dmc_base_spot spot = dmc_next_base_spot();
    assert(spot.index == -1 && spot.title != NULL && spot.title[0] == '\0');
    dmc_base_spot_free(&spot);
    dmc_room_change change = dmc_after_new_room("st006.pac", "", "");
    assert(change.find_effects != NULL && change.find_config != NULL);
    dmc_room_change_free(&change);
    char* label = dmc_preset_label("st001.pac");
    assert(label != NULL);
    dmc_string_free(label);
    dmc_menu* ops = dmc_base_room_menu(false);
    assert(ops != NULL && dmc_menu_count(ops) == 4 && dmc_menu_at(ops, 4) == NULL);
    for (size_t i = 0; i < dmc_menu_count(ops); ++i) {
        const dmc_menu_item* item = dmc_menu_at(ops, i);
        assert(item->label != NULL && strlen(item->label) > 0 && item->icon != NULL);
        assert(!dmc_menu_ready(ops, item->id));  /* nothing loaded */
    }
    assert(dmc_menu_title(ops) != NULL && dmc_menu_message(ops) != NULL);
    dmc_menu_free(ops);
    assert(dmc_room_effects_load("junk.pac", junk, sizeof junk, 0) == NULL);
    dmc_room_effects_clear(0);
    dmc_room_effects_clear(1);
    dmc_outcome config = dmc_stage_config_load("junk.pac", junk, sizeof junk, 0);
    assert(!config.ok && config.message != NULL && strlen(config.message) > 0);
    dmc_outcome_free(&config);
    dmc_stage_config_clear(0);
    char* fx = NULL;
    char* cfg = NULL;
    assert(dmc_stage_archives("st006.pac", &fx, &cfg) && fx != NULL && cfg != NULL);
    assert(strstr(fx, "006") != NULL && strstr(cfg, "006") != NULL);
    dmc_string_free(fx);
    dmc_string_free(cfg);
    assert(!dmc_stage_archives("tri.mod", NULL, NULL));
    assert(dmc_stage_slot(model.session) == 0);
    float start_view[3] = {9, 9, 9};
    assert(!dmc_stage_start_view(model.session, start_view) && start_view[0] == 9);
    char* stem = dmc_stage_stem(model.session);
    assert(stem != NULL && stem[0] == '\0');
    dmc_string_free(stem);
    assert(dmc_stage_collision_source_count(model.session) == 0);
    dmc_collision_kind kinds[4];
    assert(dmc_collision_kinds(model.session, kinds, 4) == 0);
    int ids[4];
    assert(dmc_collision_attack_ids(model.session, ids, 4) == 0);
    char* selected = dmc_select_collision_attack(model.session, 5);
    assert(selected != NULL && selected[0] == '\0');
    dmc_string_free(selected);
    dmc_report* hits = dmc_collision_report(model.session, false);
    assert(hits != NULL && strlen(dmc_report_title(hits)) > 0 && dmc_report_group_at(hits, dmc_report_group_count(hits)) == NULL);
    dmc_report_free(hits);
    dmc_report* attacks = dmc_collision_report(model.session, true);
    assert(attacks != NULL && strlen(dmc_report_title(attacks)) > 0);
    dmc_report_free(attacks);
    assert(dmc_marker_card(0) == NULL);  /* no configuration, no markers */
    dmc_session* card_file = (dmc_session*)1;
    dmc_outcome card_outcome = dmc_run_card_action(0, 1, &card_file);
    assert(card_file == NULL && card_outcome.message != NULL);
    dmc_outcome_free(&card_outcome);

    /* Group 5: the companion menu of a model: rows exist, every item has a
     * label; the effect and sound menus of a model without them. */
    dmc_shell_facts facts;
    memset(&facts, 0, sizeof facts);
    facts.root_scene = true;
    dmc_companion* companion = dmc_companion_menu(model.session, &facts);
    assert(companion != NULL && dmc_companion_get(companion, 4) == NULL && dmc_companion_get(companion, -1) == NULL);
    const dmc_menu* rows = dmc_companion_get(companion, DMC_COMPANION_ROWS);
    assert(rows != NULL && dmc_menu_count(rows) > 0);
    for (int part = 0; part < 4; ++part) {
        const dmc_menu* m = dmc_companion_get(companion, part);
        for (size_t i = 0; i < dmc_menu_count(m); ++i) assert(strlen(dmc_menu_at(m, i)->label) > 0);
    }
    dmc_companion_free(companion);
    dmc_companion* no_facts = dmc_companion_menu(model.session, NULL);
    assert(no_facts != NULL);
    dmc_companion_free(no_facts);
    dmc_menu* effects = dmc_effect_menu(model.session);
    assert(effects != NULL);
    dmc_menu_free(effects);
    dmc_outcome played = dmc_play_effect(model.session, 12345);
    assert(played.message != NULL);
    dmc_outcome_free(&played);
    dmc_menu* sounds = dmc_sound_menu(model.session);
    assert(sounds != NULL && dmc_menu_count(sounds) == 0);
    dmc_menu_free(sounds);
    dmc_pcm pcm = dmc_sound_pcm(model.session, 0);
    assert(pcm.sample_count == 0 && pcm.samples == NULL && pcm.message != NULL && strlen(pcm.message) > 0);
    dmc_pcm_free(&pcm);
    dmc_session* action_file = (dmc_session*)1;
    dmc_outcome nothing_chosen = dmc_run_companion_action(model.session, -12345, &action_file);
    assert(action_file == NULL && nothing_chosen.message != NULL);
    dmc_outcome_free(&nothing_chosen);

    /* Groups 6 and 7: a model is no cutscene; a video frame has the plan's size. */
    assert(!dmc_camera_follows_cuts(model.session));
    uint32_t rate = 7, channels = 7;
    assert(!dmc_cutscene_audio_format(model.session, &rate, &channels) && rate == 7);
    int16_t audio[64];
    assert(dmc_cutscene_audio_read(model.session, 0, 16, audio, 64) == 0);
    dmc_subtitle subtitle;
    assert(!dmc_cutscene_subtitle(model.session, &subtitle) && subtitle.text == NULL);
    dmc_subtitle_free(&subtitle);
    float take[3];
    assert(!dmc_cutscene_take_camera(model.session, take));
    assert(dmc_cutscene_vibration(model.session, 10.0f) == 0);
    dmc_outcome ogg = dmc_cutscene_sound_load(model.session, "x.ogg", junk, sizeof junk);
    assert(!ogg.ok && ogg.message != NULL);
    dmc_outcome_free(&ogg);
    dmc_video_begin();
    int vw = 0, vh = 0;
    assert(dmc_video_frame_size(W, H, &vw, &vh) && vw > 0 && vh > 0);
    uint8_t* video_frame = calloc((size_t)vw * (size_t)vh * 4, 1);
    assert(video_frame != NULL);
    assert(dmc_video_frame(model.session, W, H, &view, video_frame, 8) == 0);  /* too small */
    assert(dmc_video_frame(model.session, W, H, &view, video_frame, (size_t)vw * (size_t)vh * 4) == 1);
    free(video_frame);

    dmc_close(model.session);
    free(pixels);
    dmc_settings_free(schema);

    /* No motions here, one staged one. */
    const char* staged_motions[1] = {"walk.mot"};
    dmc_motion_strip* strip = dmc_motion_strip_build(opened.session, true, staged_motions, 1);
    assert(strip != NULL && dmc_motion_strip_count(strip) == 1);
    const dmc_motion_card* card = dmc_motion_strip_card(strip, 0);
    assert(card->staged_index == 0 && card->library_index == -1 && strcmp(card->kind, "MOT") == 0);
    assert(strcmp(card->stem, "WALK") == 0 && card->script_count == 0);
    dmc_motion_strip_free(strip);

    assert(dmc_child_count(opened.session) == 0 && dmc_open_child(opened.session, 0) == NULL);
    (void)dmc_state_flags(opened.session);
    dmc_close(opened.session);
    dmc_close(NULL);
    return 0;
}
