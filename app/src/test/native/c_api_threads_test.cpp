// The C API's thread contract: frames drawn on one thread while another
// changes the same session (parts attached and reset, part lists read) never
// overlap; group 2's picks, long presses, metrics and captures in between. Run under
// ThreadSanitizer this fails on any unlocked access.
#include "dmc_reader.h"

#include <atomic>
#include <chrono>
#include <cassert>
#include <cmath>
#include <cstring>
#include <thread>
#include <vector>

namespace {

void put_u32(std::uint8_t* bytes, std::size_t at, std::uint32_t value) {
    for (int i = 0; i < 4; ++i) bytes[at + i] = static_cast<std::uint8_t>(value >> (8 * i));
}
void put_u16(std::uint8_t* bytes, std::size_t at, std::uint16_t value) {
    bytes[at] = static_cast<std::uint8_t>(value);
    bytes[at + 1] = static_cast<std::uint8_t>(value >> 8);
}
void put_f32(std::uint8_t* bytes, std::size_t at, float value) {
    std::uint32_t raw;
    std::memcpy(&raw, &value, sizeof raw);
    put_u32(bytes, at, raw);
}
void put_u64(std::uint8_t* bytes, std::size_t at, std::uint64_t value) {
    put_u32(bytes, at, static_cast<std::uint32_t>(value));
    put_u32(bytes, at + 4, static_cast<std::uint32_t>(value >> 32));
}

// The same textureless triangle MOD as c_api_test.c.
std::vector<std::uint8_t> make_mod() {
    std::vector<std::uint8_t> bytes(0x240, 0);
    std::memcpy(bytes.data(), "MOD ", 4);
    auto* b = bytes.data();
    put_f32(b, 0x04, 1.01F);
    b[0x10] = 1;
    b[0x11] = 1;
    put_u64(b, 0x20, 0x200);
    b[0x40] = 1;
    put_u16(b, 0x42, 3);
    put_u64(b, 0x48, 0x80);
    put_u16(b, 0x80, 3);
    put_u16(b, 0x82, 5);
    put_u16(b, 0x84, 1);
    put_u16(b, 0x86, 2);
    put_u16(b, 0x88, 3);
    put_u16(b, 0x8A, 4);
    put_u64(b, 0x90, 0xD0);
    put_u64(b, 0x98, 0x100);
    put_u64(b, 0xA0, 0x130);
    put_u64(b, 0xA8, 0x140);
    put_u64(b, 0xB0, 0x150);
    put_u64(b, 0xC0, 0xE0);
    put_f32(b, 0xDC, 1.0F);
    put_f32(b, 0xEC, 1.0F);
    for (std::size_t i = 0; i < 3; ++i) put_f32(b, 0x100 + i * 12 + 8, 1.0F);
    put_u16(b, 0x134, 4096);
    put_u16(b, 0x13A, 4096);
    put_u16(b, 0x150, 0x001F);
    put_u16(b, 0x152, 0x001F);
    put_u16(b, 0x154, 0x001F);
    put_u32(b, 0x200, 0x10);
    put_u32(b, 0x204, 0x20);
    put_u32(b, 0x208, 0x30);
    b[0x210] = 0xFF;
    return bytes;
}

}  // namespace

int main() {
    const auto mod = make_mod();
    dmc_session* base = dmc_open_source("base.mod", mod.data(), mod.size());
    dmc_session* extra = dmc_open_source("extra.mod", mod.data(), mod.size());
    assert(base != nullptr && extra != nullptr);
    const dmc_session* sources[2] = {base, extra};
    const char* names[2] = {"base.mod", "extra.mod"};
    dmc_session* composite = dmc_compose(sources, names, 2);
    assert(composite != nullptr);

    const char* key = "set.gpu";
    const float cpu = 0.0F;
    const auto applied = dmc_apply_settings(&key, &cpu, 1);
    dmc_view view{};
    view.zoom = 1.0F;
    view.render_flags = applied.render_flags;
    view.motion_frame = NAN;

    constexpr int kSide = DMC_RENDER_MIN_SIDE;
    std::atomic<bool> done{false};
    std::atomic<int> frames{0};
    std::thread render([&] {
        std::vector<std::uint8_t> pixels(static_cast<std::size_t>(kSide) * kSide * 4U);
        while (!done.load()) {
            if (dmc_render(composite, kSide, kSide, &view, pixels.data(), pixels.size()) == 1) ++frames;
            // A view draws at its frame pace; drawing back to back would keep
            // the lock from the other thread (a mutex promises no turn order).
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    });
    for (int round = 0; round < 200; ++round) {
        char* attached = dmc_part_attach(composite, 0, 1, 0);
        dmc_string_free(attached);
        dmc_parts* parts = dmc_parts_list(composite);
        assert(parts != nullptr && dmc_parts_count(parts) == 2);
        dmc_parts_free(parts);
        char* reset = dmc_part_reset(composite, 1);
        assert(reset != nullptr && std::strcmp(reset, "reset") == 0);
        dmc_string_free(reset);
        (void)dmc_set_info_preview(composite, (round & 1) != 0);
        char* info = dmc_info_text(composite, "composite", 0, nullptr, nullptr, 0);
        dmc_string_free(info);
        // Group 2 between frames: picks, a long press, metrics, a capture (poses the session).
        dmc_pick_result pick = dmc_pick(composite, kSide, kSide, &view, 32.0F, 32.0F, false);
        dmc_pick_result_free(&pick);
        dmc_outcome held = dmc_hold(composite, kSide, kSide, &view, 32.0F, 32.0F, true, false);
        dmc_outcome_free(&held);
        float distance = 0.0F, dolly = 0.0F;
        assert(dmc_camera_metrics(composite, &distance, &dolly) && distance > 0.0F);
        // Group 3 between frames: a motion load (refused here) and the range read.
        const std::uint8_t junk[8] = {1, 2, 3, 4, 5, 6, 7, 8};
        char* report = dmc_load_motion(composite, "junk.mot", junk, sizeof junk);
        dmc_string_free(report);
        float end_frame = 0.0F, loop_start = 0.0F;
        (void)dmc_motion_range(composite, &end_frame, &loop_start);
        if (round % 50 == 0) {
            dmc_image* photo = dmc_render_capture(composite, false, kSide, kSide, &view, 30.0F);
            dmc_image_free(photo);
        }
    }
    while (frames.load() < 3) std::this_thread::yield();
    done = true;
    render.join();

    dmc_close(composite);
    dmc_close(extra);
    dmc_close(base);
    return 0;
}
