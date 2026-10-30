#include "dmcresource/frame_profile.h"

#include <atomic>
#include <cstdio>

namespace dmcresource::frame_profile {
namespace {

std::array<std::atomic<std::uint64_t>, static_cast<std::size_t>(Stage::Count)> g_ns{};
std::atomic<std::uint64_t> g_frames{0U};

}  // namespace

void add(Stage stage, std::chrono::nanoseconds time) noexcept {
    if (stage >= Stage::Count || time.count() <= 0) return;
    g_ns[static_cast<std::size_t>(stage)].fetch_add(static_cast<std::uint64_t>(time.count()),
                                                    std::memory_order_relaxed);
}

void frame_done() noexcept { g_frames.fetch_add(1U, std::memory_order_relaxed); }

void reset() noexcept {
    for (auto& ns : g_ns) ns.store(0U, std::memory_order_relaxed);
    g_frames.store(0U, std::memory_order_relaxed);
}

Totals totals() noexcept {
    Totals out;
    out.frames = g_frames.load(std::memory_order_relaxed);
    for (std::size_t i = 0; i < out.ms.size(); ++i) {
        out.ms[i] = static_cast<double>(g_ns[i].load(std::memory_order_relaxed)) / 1.0e6;
    }
    return out;
}

const char* name(Stage stage) noexcept {
    switch (stage) {
        case Stage::Pose: return "pose";
        case Stage::Prepare: return "prepare";
        case Stage::GpuBuild: return "GPU build";
        case Stage::GpuDraw: return "GPU draw";
        case Stage::Readback: return "read back";
        case Stage::CpuRaster: return "CPU raster";
        case Stage::Overlays: return "overlays";
        case Stage::Camera: return "camera";
        case Stage::CopyOut: return "copy out";
        case Stage::Count: break;
    }
    return "?";
}

std::string per_frame_line() {
    const auto t = totals();
    if (t.frames == 0U) return {};
    std::string line = "Per frame (ms):";
    bool first = true;
    for (std::size_t i = 0; i < t.ms.size(); ++i) {
        const double ms = t.ms[i] / static_cast<double>(t.frames);
        if (ms < 0.05) continue;
        char text[48];
        std::snprintf(text, sizeof(text), "%s %s %.1f", first ? "" : " \xC2\xB7", name(static_cast<Stage>(i)), ms);
        line += text;
        first = false;
    }
    return line;
}

}  // namespace dmcresource::frame_profile
