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
    assert(dmc_render(model.session, W, H, &view, pixels, (size_t)W * H * 4) == 1);
    size_t differing = 0;
    for (size_t i = 0; i < (size_t)W * H; ++i) {
        if (memcmp(pixels + i * 4, pixels, 4) != 0) ++differing;
    }
    assert(differing > 0);  /* not one flat background */
    assert(dmc_render(opened.session, W, H, &view, pixels, (size_t)W * H * 4) == 0);  /* a texture: no 3D view */
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
