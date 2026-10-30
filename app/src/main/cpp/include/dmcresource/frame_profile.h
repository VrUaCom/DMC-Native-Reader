#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <string>

// Where a frame's time goes: each stage of the viewer's frame adds its time
// here (thread-safe, a few nanoseconds a stage), and the benchmark reports
// the average per frame. Stages on the GPU are counted where the CPU waits
// for them: "GPU draw" is submitting the work, "read back" includes the GPU
// finishing it.
namespace dmcresource::frame_profile {

enum class Stage : std::uint8_t {
    Pose,       // the motion posed (cloth, cutscene)
    Prepare,    // the view prepared: camera, presentation mesh, effects, room
    GpuBuild,   // the GPU frame's geometry written down
    GpuDraw,    // GL calls and uploads
    Readback,   // the GPU finishing and the pixels read back
    CpuRaster,  // the software renderer's triangles (CPU frames)
    Overlays,   // lines, markers, labels drawn over the frame
    Camera,     // the photo camera: lens, colour, depth of field
    CopyOut,    // the pixels copied to the shell
    Count,
};

void add(Stage stage, std::chrono::nanoseconds time) noexcept;
// One frame finished (the averages divide by this).
void frame_done() noexcept;
void reset() noexcept;

struct Totals final {
    std::array<double, static_cast<std::size_t>(Stage::Count)> ms{};
    std::uint64_t frames{};
};
[[nodiscard]] Totals totals() noexcept;
[[nodiscard]] const char* name(Stage stage) noexcept;
// "Per frame (ms): pose 1.2 · prepare 3.4 · ..." with only the stages that took time.
[[nodiscard]] std::string per_frame_line();

// Times its scope into a stage.
class Scope final {
public:
    explicit Scope(Stage stage) noexcept : stage_{stage}, start_{std::chrono::steady_clock::now()} {}
    ~Scope() { add(stage_, std::chrono::steady_clock::now() - start_); }
    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;

private:
    Stage stage_;
    std::chrono::steady_clock::time_point start_;
};

}  // namespace dmcresource::frame_profile
