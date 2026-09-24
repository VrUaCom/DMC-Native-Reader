#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "dmcresource/collision_shapes.h"
#include "dmcresource/image_preview.h"
#include "dmcresource/inspection_document.h"
#include "dmcresource/motion/cloth_chain.h"
#include "dmcresource/motion/motion_script.h"
#include "dmcresource/motion/uv_scroll.h"

// Stand-alone 2D views of files that have no mesh or pixels of their own. Each
// view draws what the file holds with the same ports the 3D playback uses:
//   TSC  - U/V offset of every scroll record over 240 game frames (step_scroll)
//          plus a scrolled checker strip per record;
//   CLT  - every chain block as its bone list (node + axis), gravity and wind
//          arrows and the solver parameters;
//   motion script - banks (pl000_00_N) with script counts, played MOT ids,
//          wait blocks and the weapon attach states each bank uses;
//   binary - byte profile of an unknown payload: entropy strip per block, byte
//          histogram, printable strings, candidate offset tables, hex dump.
namespace dmcresource::views {

inline constexpr int kViewWidth = 1080;
inline constexpr int kViewHeight = 1440;

[[nodiscard]] ImagePreview render_tsc_view(const std::vector<motion::ScrollRecord>& records);
[[nodiscard]] ImagePreview render_clt_view(const std::vector<motion::ClothParams>& blocks);
[[nodiscard]] ImagePreview render_motion_script_view(const motion::MotionScriptFile& file);

// Collision shapes in bone space: front (X/Y) and side (Z/Y) projections,
// spheres as circles, capsules as two circles joined, boxes as their
// rotated outline; each labelled with its record index.
[[nodiscard]] ImagePreview render_collision_view(const std::vector<collision::Shape>& shapes);

// Attack index: every entry (id, target mask, bone, shape).
[[nodiscard]] ImagePreview render_attack_index_view(const std::vector<collision::AttackEntry>& entries);

struct BinaryProfile final {
    std::size_t size{};
    double entropy{};           // bits per byte over the whole file
    double zero_ratio{};
    double printable_ratio{};
    std::vector<std::string> strings;       // printable runs >= 5 chars (first 32)
    std::vector<std::size_t> string_offsets;
    std::string fourcc;                     // first four bytes when printable
    std::uint32_t u32_0{}, u32_4{}, u32_8{}, u32_12{};
    // Leading u32 run that looks like an ascending offset table (< size).
    std::size_t offset_table_entries{};
    // Aligned u32 words that read as ordinary float32 values (|v| in
    // [1e-4, 1e5]) among the non-zero words.
    double float_ratio{};
    // Record stride guess: the step (4..512) at which non-zero bytes repeat
    // best inside the first 64 KiB; 0 when nothing repeats.
    std::size_t stride{};
    double stride_score{};
    std::vector<float> block_entropy;       // per block of `block_size`
    std::size_t block_size{};
    std::vector<std::uint32_t> histogram;   // 256 bins
};

[[nodiscard]] BinaryProfile profile_binary(std::span<const std::uint8_t> bytes);

// Adds the profile to an inspection document (root properties + a
// "Strings" child).
void append_binary_inspection(InspectionDocument& inspection, const BinaryProfile& profile);

[[nodiscard]] ImagePreview render_binary_view(const InspectionDocument& inspection,
                                              std::string_view detail,
                                              const BinaryProfile& profile,
                                              std::span<const std::uint8_t> bytes);

}  // namespace dmcresource::views
