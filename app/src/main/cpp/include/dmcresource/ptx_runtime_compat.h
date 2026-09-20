#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>

namespace dmcresource::ptx_runtime_compat {

// Reader-owned C++23 compatibility model for the EXE-confirmed PTX runtime
// placement/pool slice recovered in read-only Rengine commit
// 50d070e158e484937238d9cb02b2bc6affb2f502.
//
// This is runtime state, NOT a serialized PTX parser. The existing
// TextureSlotFramingParser -> TextureSet -> DDS path remains the only disk-file
// authority. Raw EXE virtual addresses are deliberately absent from this API.

enum class RuntimeError : std::uint8_t {
    not_initialized,
    allocation_failed,
    invalid_record,
    invalid_length,
    memory_out_of_range,
    invalid_manager_entry,
};

enum class PlacementStatus : std::uint8_t {
    placed,
    rejected,
};

struct RuntimeConfig final {
    // Raw unsigned words read by the recovered runtime path at graphics config
    // +0x4C/+0x4E. The containing graphics-config type remains unresolved.
    std::uint16_t word_4c{};
    std::uint16_t word_4e{};
};

struct RecordHandle final {
    std::uint16_t index{};
};

// Explicit already-materialized runtime record fields used by this first
// compatibility slice. Offset-shaped names are intentional: a proven mapping
// from serialized TextureSet::Slot fields to these runtime fields does not yet
// exist and must not be invented.
struct RecordFields final {
    std::int16_t length_0e{};
    std::uint16_t word_18{};
    std::int16_t signed_1c{-1};
    std::int16_t length_26{};
    std::uint16_t word_44{};
};

struct RecordView final {
    std::uint16_t units_06{};
    std::int16_t length_0e{};
    std::uint16_t word_18{};
    std::int16_t signed_1c{};
    std::uint16_t units_1e{};
    std::int16_t length_26{};
    std::uint16_t word_44{};
};

struct PoolView final {
    std::uint32_t base_units{};
    std::uint32_t scan_limit_units{};
    std::uint32_t upper_units{};
    std::uint32_t reserved_units{};
    std::uint32_t remaining_units{};
};

struct ManagerEntryView final {
    std::uint64_t resource_key{};
    std::uint32_t references{};
    std::uint32_t preserved_0c_marker{};
    std::uint64_t payload_marker{};
};

class PtxRuntimeCompat final {
public:
    PtxRuntimeCompat() noexcept;
    ~PtxRuntimeCompat();

    PtxRuntimeCompat(PtxRuntimeCompat&&) noexcept;
    PtxRuntimeCompat& operator=(PtxRuntimeCompat&&) noexcept;

    PtxRuntimeCompat(const PtxRuntimeCompat&) = delete;
    PtxRuntimeCompat& operator=(const PtxRuntimeCompat&) = delete;

    [[nodiscard]] bool has_state() const noexcept;

    // EXE 0x140331910 projected into bounded Reader-owned state. Pool bytes are
    // cleared; existing manager state is not implicitly released/reset here.
    [[nodiscard]] std::expected<void, RuntimeError> initialize(
        RuntimeConfig config) noexcept;

    // EXE 0x140331D90. Updates reservation/bounds and resets manager keys/counts
    // while preserving pool records/occupancy/tail and manager payload/padding.
    [[nodiscard]] std::expected<void, RuntimeError> configure_reservation(
        std::uint32_t blocks,
        RuntimeConfig config) noexcept;

    // Host inspection fixture: populate only the already-materialized record
    // fields consumed by the recovered placement function. This does NOT claim
    // serialized PTX -> runtime-record materialization semantics.
    [[nodiscard]] std::expected<void, RuntimeError> seed_record_for_inspection(
        RecordHandle record,
        RecordFields fields) noexcept;

    // Host inspection fixture for represented occupancy state. It is not the
    // EXE marker routine and must not be interpreted as such.
    [[nodiscard]] std::expected<void, RuntimeError> set_allocation_cell_for_inspection(
        std::uint32_t cell,
        bool occupied) noexcept;

    // Host inspection fixture used to prove configure/reset preservation.
    [[nodiscard]] std::expected<void, RuntimeError> seed_manager_entry_for_inspection(
        std::size_t index,
        std::uint64_t resource_key,
        std::uint32_t references,
        std::uint32_t preserved_0c_marker,
        std::uint64_t payload_marker) noexcept;

    // EXE 0x140331520. Normal EXE-style failure/no-space is PlacementStatus::rejected;
    // host-model invalid represented memory/length is RuntimeError.
    [[nodiscard]] std::expected<PlacementStatus, RuntimeError> place_record(
        RecordHandle record,
        std::int32_t requested_index,
        std::int32_t length_mode,
        RuntimeConfig config) noexcept;

    [[nodiscard]] std::expected<RecordView, RuntimeError> inspect_record(
        RecordHandle record) const noexcept;
    [[nodiscard]] std::expected<PoolView, RuntimeError> inspect_pool() const noexcept;
    [[nodiscard]] std::expected<ManagerEntryView, RuntimeError> inspect_manager_entry(
        std::size_t index) const noexcept;

private:
    struct State;
    std::unique_ptr<State> state_;
};

}  // namespace dmcresource::ptx_runtime_compat
