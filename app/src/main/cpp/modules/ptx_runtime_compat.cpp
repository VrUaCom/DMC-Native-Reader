#include "dmcresource/ptx_runtime_compat.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>

namespace dmcresource::ptx_runtime_compat {
namespace {

// Provenance: Reader-owned C++23 projection of the EXE-confirmed PTX runtime
// slice recovered in read-only dmc-rengine-cpp commit
// 50d070e158e484937238d9cb02b2bc6affb2f502:
//   0x140331520 placement
//   0x140331910 pool initialization
//   0x140331D90 reservation/configure
// and the reset-key behavior called by configure (0x140315150).
//
// Keep this representation private. It models the represented runtime memory
// required by the confirmed algorithms; it is not a serialized PTX layout.

inline constexpr std::size_t kRecordSize = 0x50U;
inline constexpr std::size_t kRecordCount = 128U;
inline constexpr std::size_t kAllocationMapOffset = 0x2800U;
inline constexpr std::size_t kPoolBaseUnitsOffset = 0xcb08U;
inline constexpr std::size_t kPoolLimitUnitsOffset = 0xcb0cU;
inline constexpr std::size_t kPoolUpperUnitsOffset = 0xcb10U;
inline constexpr std::size_t kPoolReservedUnitsOffset = 0xcb14U;
inline constexpr std::size_t kPoolRemainingUnitsOffset = 0xcb18U;
inline constexpr std::size_t kPoolStorageSize = 0xcb50U;
inline constexpr std::size_t kManagerEntryCount = 32U;
inline constexpr std::size_t kOpaquePayloadSize = 0x208U;

struct ManagerEntryImage final {
    std::uint64_t resource_key{};
    std::uint32_t references{};
    std::array<std::byte, 4> preserved_0c{};
    std::array<std::byte, kOpaquePayloadSize> payload{};
};
static_assert(sizeof(ManagerEntryImage) == 0x218U);

struct ManagerImage final {
    // Opaque 8-byte prefix retained so the represented entry offsets match the
    // recovered manager image. No executable vtable is exposed or invoked.
    std::uint64_t opaque_prefix{};
    std::array<ManagerEntryImage, kManagerEntryCount> entries{};
};
static_assert(sizeof(ManagerImage) == 0x4308U);

template <typename T, std::size_t N>
[[nodiscard]] std::expected<T, RuntimeError> read_at(
    const std::array<std::byte, N>& bytes,
    std::size_t offset) noexcept {
    if (offset > bytes.size() || sizeof(T) > bytes.size() - offset) {
        return std::unexpected(RuntimeError::memory_out_of_range);
    }
    T value{};
    std::memcpy(&value, bytes.data() + offset, sizeof(value));
    return value;
}

template <typename T, std::size_t N>
[[nodiscard]] std::expected<void, RuntimeError> write_at(
    std::array<std::byte, N>& bytes,
    std::size_t offset,
    T value) noexcept {
    if (offset > bytes.size() || sizeof(T) > bytes.size() - offset) {
        return std::unexpected(RuntimeError::memory_out_of_range);
    }
    std::memcpy(bytes.data() + offset, &value, sizeof(value));
    return {};
}

[[nodiscard]] constexpr std::int32_t wrap_add(
    std::int32_t a,
    std::int32_t b) noexcept {
    return std::bit_cast<std::int32_t>(
        static_cast<std::uint32_t>(a) + static_cast<std::uint32_t>(b));
}

[[nodiscard]] constexpr std::uint16_t units(std::int32_t index) noexcept {
    return static_cast<std::uint16_t>(static_cast<std::uint32_t>(index) << 5U);
}

[[nodiscard]] std::expected<std::size_t, RuntimeError> record_offset(
    RecordHandle record) noexcept {
    if (record.index >= kRecordCount) {
        return std::unexpected(RuntimeError::invalid_record);
    }
    return static_cast<std::size_t>(record.index) * kRecordSize;
}

void reset_manager_keys(ManagerImage& manager) noexcept {
    for (auto& entry : manager.entries) {
        entry.resource_key = 0U;
        entry.references = 0U;
    }
}

}  // namespace

struct PtxRuntimeCompat::State final {
    std::array<std::byte, kPoolStorageSize> pool{};
    ManagerImage manager{};
};

PtxRuntimeCompat::PtxRuntimeCompat() noexcept = default;
PtxRuntimeCompat::~PtxRuntimeCompat() = default;
PtxRuntimeCompat::PtxRuntimeCompat(PtxRuntimeCompat&&) noexcept = default;
PtxRuntimeCompat& PtxRuntimeCompat::operator=(PtxRuntimeCompat&&) noexcept = default;

bool PtxRuntimeCompat::has_state() const noexcept {
    return state_ != nullptr;
}

std::expected<void, RuntimeError> PtxRuntimeCompat::initialize(
    RuntimeConfig config) noexcept {
    if (!state_) {
        try {
            state_ = std::make_unique<State>();
        } catch (...) {
            return std::unexpected(RuntimeError::allocation_failed);
        }
    }

    // 0x140331910 clears the represented 0xCB50-byte pool extent. It does not
    // perform a manager release/reset in the recovered function.
    state_->pool.fill(std::byte{0});

    if (auto result = write_at(
            state_->pool,
            kPoolBaseUnitsOffset,
            static_cast<std::uint32_t>(config.word_4c));
        !result) {
        return result;
    }
    if (auto result = write_at(
            state_->pool,
            kPoolUpperUnitsOffset,
            static_cast<std::uint32_t>(config.word_4e));
        !result) {
        return result;
    }

    const auto remaining =
        static_cast<std::uint32_t>(config.word_4e) -
        static_cast<std::uint32_t>(config.word_4c);
    return write_at(state_->pool, kPoolRemainingUnitsOffset, remaining);
}

std::expected<void, RuntimeError> PtxRuntimeCompat::configure_reservation(
    std::uint32_t blocks,
    RuntimeConfig config) noexcept {
    if (!state_) {
        return std::unexpected(RuntimeError::not_initialized);
    }

    // Preserve modulo-2^32 behavior of the recovered blocks << 5 path.
    const auto reserved = static_cast<std::uint32_t>(blocks << 5U);
    const auto limit = static_cast<std::uint32_t>(config.word_4e) - reserved;
    const auto remaining = limit - static_cast<std::uint32_t>(config.word_4c);

    if (auto result = write_at(state_->pool, kPoolReservedUnitsOffset, reserved);
        !result) {
        return result;
    }
    if (auto result = write_at(state_->pool, kPoolLimitUnitsOffset, limit);
        !result) {
        return result;
    }
    if (auto result = write_at(state_->pool, kPoolUpperUnitsOffset, limit);
        !result) {
        return result;
    }
    if (auto result = write_at(state_->pool, kPoolRemainingUnitsOffset, remaining);
        !result) {
        return result;
    }

    // Confirmed dependency: keys/reference counts reset, payload/padding remain.
    reset_manager_keys(state_->manager);
    return {};
}

std::expected<void, RuntimeError> PtxRuntimeCompat::seed_record_for_inspection(
    RecordHandle record,
    RecordFields fields) noexcept {
    if (!state_) {
        return std::unexpected(RuntimeError::not_initialized);
    }
    const auto offset = record_offset(record);
    if (!offset) {
        return std::unexpected(offset.error());
    }

    std::fill_n(state_->pool.begin() + static_cast<std::ptrdiff_t>(*offset),
                kRecordSize,
                std::byte{0});

    if (auto result = write_at(state_->pool, *offset + 0x0eU, fields.length_0e);
        !result) {
        return result;
    }
    if (auto result = write_at(state_->pool, *offset + 0x18U, fields.word_18);
        !result) {
        return result;
    }
    if (auto result = write_at(state_->pool, *offset + 0x1cU, fields.signed_1c);
        !result) {
        return result;
    }
    if (auto result = write_at(state_->pool, *offset + 0x26U, fields.length_26);
        !result) {
        return result;
    }
    return write_at(state_->pool, *offset + 0x44U, fields.word_44);
}

std::expected<void, RuntimeError> PtxRuntimeCompat::set_allocation_cell_for_inspection(
    std::uint32_t cell,
    bool occupied) noexcept {
    if (!state_) {
        return std::unexpected(RuntimeError::not_initialized);
    }

    // Limit fixture mutation to the represented allocation-map region before
    // the confirmed numeric pool fields. The recovered placement itself has
    // its own branch-specific bounds/omissions, preserved in place_record().
    constexpr auto kAllocationMapBytes = kPoolBaseUnitsOffset - kAllocationMapOffset;
    if (cell >= kAllocationMapBytes) {
        return std::unexpected(RuntimeError::memory_out_of_range);
    }
    state_->pool[kAllocationMapOffset + cell] =
        occupied ? std::byte{1} : std::byte{0};
    return {};
}

std::expected<void, RuntimeError> PtxRuntimeCompat::seed_manager_entry_for_inspection(
    std::size_t index,
    std::uint64_t resource_key,
    std::uint32_t references,
    std::uint32_t preserved_0c_marker,
    std::uint64_t payload_marker) noexcept {
    if (!state_) {
        return std::unexpected(RuntimeError::not_initialized);
    }
    if (index >= state_->manager.entries.size()) {
        return std::unexpected(RuntimeError::invalid_manager_entry);
    }

    auto& entry = state_->manager.entries[index];
    entry.resource_key = resource_key;
    entry.references = references;
    std::memcpy(entry.preserved_0c.data(),
                &preserved_0c_marker,
                sizeof(preserved_0c_marker));
    std::memcpy(entry.payload.data(), &payload_marker, sizeof(payload_marker));
    return {};
}

std::expected<PlacementStatus, RuntimeError> PtxRuntimeCompat::place_record(
    RecordHandle record,
    std::int32_t requested_index,
    std::int32_t length_mode,
    RuntimeConfig config) noexcept {
    if (!state_) {
        return std::unexpected(RuntimeError::not_initialized);
    }
    const auto offset = record_offset(record);
    if (!offset) {
        return std::unexpected(offset.error());
    }

    const auto signed_1c = read_at<std::int16_t>(state_->pool, *offset + 0x1cU);
    const auto word_44 = read_at<std::uint16_t>(state_->pool, *offset + 0x44U);
    const auto word_18 = read_at<std::uint16_t>(state_->pool, *offset + 0x18U);
    if (!signed_1c) return std::unexpected(signed_1c.error());
    if (!word_44) return std::unexpected(word_44.error());
    if (!word_18) return std::unexpected(word_18.error());

    const bool palette = *signed_1c >= 0 && *word_44 == 0U && *word_18 == 0U;

    std::int32_t image_length = 0;
    std::int32_t palette_length = 0;
    if (length_mode == 0) {
        image_length = 1;
        if (auto result = write_at(
                state_->pool, *offset + 0x0eU, std::int16_t{1});
            !result) {
            return std::unexpected(result.error());
        }
        if (palette) {
            palette_length = 1;
            if (auto result = write_at(
                    state_->pool, *offset + 0x26U, std::int16_t{1});
                !result) {
                return std::unexpected(result.error());
            }
        }
    } else {
        const auto image = read_at<std::int16_t>(state_->pool, *offset + 0x0eU);
        if (!image) return std::unexpected(image.error());
        image_length = *image;
        if (palette) {
            const auto extra = read_at<std::int16_t>(state_->pool, *offset + 0x26U);
            if (!extra) return std::unexpected(extra.error());
            palette_length = *extra;
        }
    }

    if (image_length <= 0 || (requested_index == 0 && palette_length < 0)) {
        return std::unexpected(RuntimeError::invalid_length);
    }

    const auto base_value = read_at<std::int32_t>(state_->pool, kPoolBaseUnitsOffset);
    if (!base_value) return std::unexpected(base_value.error());
    auto start = *base_value / 32;

    const auto read_cell = [this](std::int64_t offset_value)
        -> std::expected<std::byte, RuntimeError> {
        if (offset_value < 0 ||
            offset_value >= static_cast<std::int64_t>(state_->pool.size())) {
            return std::unexpected(RuntimeError::memory_out_of_range);
        }
        return state_->pool[static_cast<std::size_t>(offset_value)];
    };

    if (requested_index == 0) {
        const auto delta = wrap_add(
            *base_value,
            -static_cast<std::int32_t>(config.word_4c));
        std::int64_t cursor =
            static_cast<std::int64_t>(kAllocationMapOffset) + delta / 32;
        std::int32_t run = 0;

        for (;;) {
            auto cell = read_cell(cursor);
            if (!cell) return std::unexpected(cell.error());

            if (*cell != std::byte{0}) {
                const auto limit_value =
                    read_at<std::int32_t>(state_->pool, kPoolLimitUnitsOffset);
                if (!limit_value) return std::unexpected(limit_value.error());
                const auto limit = *limit_value / 32;
                auto candidate = wrap_add(run, start);

                do {
                    ++cursor;
                    run = wrap_add(run, 1);
                    candidate = wrap_add(candidate, 1);
                    if (candidate >= limit) {
                        return PlacementStatus::rejected;
                    }
                    cell = read_cell(cursor);
                    if (!cell) return std::unexpected(cell.error());
                } while (*cell != std::byte{0});

                start = wrap_add(start, run);
                run = 0;
            } else {
                ++cursor;
                run = wrap_add(run, 1);
                if (run == wrap_add(image_length, palette_length)) {
                    if (auto result = write_at(
                            state_->pool, *offset + 0x06U, units(start));
                        !result) {
                        return std::unexpected(result.error());
                    }
                    if (*word_18 == 0U) {
                        if (auto result = write_at(
                                state_->pool,
                                *offset + 0x1eU,
                                units(wrap_add(start, image_length)));
                            !result) {
                            return std::unexpected(result.error());
                        }
                    }
                    return PlacementStatus::placed;
                }
            }
        }
    }

    // Preserve the recovered two-comparison behavior. In particular, when
    // requested_index >= start the pool limit is not read at all.
    if (requested_index < start) {
        const auto limit_value =
            read_at<std::int32_t>(state_->pool, kPoolLimitUnitsOffset);
        if (!limit_value) return std::unexpected(limit_value.error());
        if (requested_index >= *limit_value / 32) {
            return PlacementStatus::rejected;
        }
    }

    std::int64_t cursor =
        static_cast<std::int64_t>(kAllocationMapOffset) + requested_index;
    for (std::int32_t remaining = image_length;
         remaining > 0;
         --remaining, ++cursor) {
        const auto cell = read_cell(cursor);
        if (!cell) return std::unexpected(cell.error());
        if (*cell != std::byte{0}) {
            return PlacementStatus::rejected;
        }
    }

    const auto chosen = wrap_add(start, requested_index);
    if (auto result = write_at(state_->pool, *offset + 0x06U, units(chosen));
        !result) {
        return std::unexpected(result.error());
    }

    const auto raw_length = read_at<std::uint16_t>(state_->pool, *offset + 0x0eU);
    if (!raw_length) return std::unexpected(raw_length.error());
    if (auto result = write_at(
            state_->pool,
            *offset + 0x1eU,
            units(wrap_add(chosen, static_cast<std::int32_t>(*raw_length))));
        !result) {
        return std::unexpected(result.error());
    }

    return PlacementStatus::placed;
}

std::expected<RecordView, RuntimeError> PtxRuntimeCompat::inspect_record(
    RecordHandle record) const noexcept {
    if (!state_) {
        return std::unexpected(RuntimeError::not_initialized);
    }
    const auto offset = record_offset(record);
    if (!offset) return std::unexpected(offset.error());

    const auto units_06 = read_at<std::uint16_t>(state_->pool, *offset + 0x06U);
    const auto length_0e = read_at<std::int16_t>(state_->pool, *offset + 0x0eU);
    const auto word_18 = read_at<std::uint16_t>(state_->pool, *offset + 0x18U);
    const auto signed_1c = read_at<std::int16_t>(state_->pool, *offset + 0x1cU);
    const auto units_1e = read_at<std::uint16_t>(state_->pool, *offset + 0x1eU);
    const auto length_26 = read_at<std::int16_t>(state_->pool, *offset + 0x26U);
    const auto word_44 = read_at<std::uint16_t>(state_->pool, *offset + 0x44U);

    if (!units_06) return std::unexpected(units_06.error());
    if (!length_0e) return std::unexpected(length_0e.error());
    if (!word_18) return std::unexpected(word_18.error());
    if (!signed_1c) return std::unexpected(signed_1c.error());
    if (!units_1e) return std::unexpected(units_1e.error());
    if (!length_26) return std::unexpected(length_26.error());
    if (!word_44) return std::unexpected(word_44.error());

    return RecordView{
        .units_06 = *units_06,
        .length_0e = *length_0e,
        .word_18 = *word_18,
        .signed_1c = *signed_1c,
        .units_1e = *units_1e,
        .length_26 = *length_26,
        .word_44 = *word_44,
    };
}

std::expected<PoolView, RuntimeError> PtxRuntimeCompat::inspect_pool() const noexcept {
    if (!state_) {
        return std::unexpected(RuntimeError::not_initialized);
    }

    const auto base = read_at<std::uint32_t>(state_->pool, kPoolBaseUnitsOffset);
    const auto limit = read_at<std::uint32_t>(state_->pool, kPoolLimitUnitsOffset);
    const auto upper = read_at<std::uint32_t>(state_->pool, kPoolUpperUnitsOffset);
    const auto reserved = read_at<std::uint32_t>(state_->pool, kPoolReservedUnitsOffset);
    const auto remaining = read_at<std::uint32_t>(state_->pool, kPoolRemainingUnitsOffset);

    if (!base) return std::unexpected(base.error());
    if (!limit) return std::unexpected(limit.error());
    if (!upper) return std::unexpected(upper.error());
    if (!reserved) return std::unexpected(reserved.error());
    if (!remaining) return std::unexpected(remaining.error());

    return PoolView{
        .base_units = *base,
        .scan_limit_units = *limit,
        .upper_units = *upper,
        .reserved_units = *reserved,
        .remaining_units = *remaining,
    };
}

std::expected<ManagerEntryView, RuntimeError>
PtxRuntimeCompat::inspect_manager_entry(std::size_t index) const noexcept {
    if (!state_) {
        return std::unexpected(RuntimeError::not_initialized);
    }
    if (index >= state_->manager.entries.size()) {
        return std::unexpected(RuntimeError::invalid_manager_entry);
    }

    const auto& entry = state_->manager.entries[index];
    std::uint32_t preserved_marker = 0U;
    std::uint64_t payload_marker = 0U;
    std::memcpy(&preserved_marker,
                entry.preserved_0c.data(),
                sizeof(preserved_marker));
    std::memcpy(&payload_marker,
                entry.payload.data(),
                sizeof(payload_marker));

    return ManagerEntryView{
        .resource_key = entry.resource_key,
        .references = entry.references,
        .preserved_0c_marker = preserved_marker,
        .payload_marker = payload_marker,
    };
}

}  // namespace dmcresource::ptx_runtime_compat
