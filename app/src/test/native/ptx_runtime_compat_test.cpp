#include <cassert>
#include <cstdint>

#include "dmcresource/ptx_runtime_compat.h"

namespace {

using dmcresource::ptx_runtime_compat::ManagerEntryView;
using dmcresource::ptx_runtime_compat::PlacementStatus;
using dmcresource::ptx_runtime_compat::PtxRuntimeCompat;
using dmcresource::ptx_runtime_compat::RecordFields;
using dmcresource::ptx_runtime_compat::RecordHandle;
using dmcresource::ptx_runtime_compat::RuntimeConfig;
using dmcresource::ptx_runtime_compat::RuntimeError;

constexpr RuntimeConfig kConfig{
    .word_4c = 64U,
    .word_4e = 320U,
};

void require_ok(const auto& result) {
    assert(result.has_value());
}

void test_initialize_and_manager_independence() {
    PtxRuntimeCompat runtime;
    assert(!runtime.has_state());

    require_ok(runtime.initialize(kConfig));
    assert(runtime.has_state());

    auto pool = runtime.inspect_pool();
    require_ok(pool);
    assert(pool->base_units == 64U);
    assert(pool->scan_limit_units == 0U);
    assert(pool->upper_units == 320U);
    assert(pool->reserved_units == 0U);
    assert(pool->remaining_units == 256U);

    require_ok(runtime.seed_manager_entry_for_inspection(
        0U,
        0x1122334455667788ULL,
        7U,
        0xaabbccddU,
        0x8877665544332211ULL));

    // The recovered 0x140331910 initializer clears pool storage only. It does
    // not implicitly reset/release the separate manager.
    require_ok(runtime.initialize(RuntimeConfig{.word_4c = 96U, .word_4e = 352U}));
    const auto manager = runtime.inspect_manager_entry(0U);
    require_ok(manager);
    assert(manager->resource_key == 0x1122334455667788ULL);
    assert(manager->references == 7U);
    assert(manager->preserved_0c_marker == 0xaabbccddU);
    assert(manager->payload_marker == 0x8877665544332211ULL);
}

void test_reservation_and_preservation() {
    PtxRuntimeCompat runtime;
    require_ok(runtime.initialize(kConfig));

    require_ok(runtime.seed_record_for_inspection(
        RecordHandle{0U},
        RecordFields{
            .length_0e = 3,
            .word_18 = 0x22U,
            .signed_1c = -1,
            .length_26 = 5,
            .word_44 = 0x33U,
        }));
    require_ok(runtime.set_allocation_cell_for_inspection(0U, true));
    require_ok(runtime.seed_manager_entry_for_inspection(
        3U,
        0x123456789abcdef0ULL,
        9U,
        0xdeadbeefU,
        0x0123456789abcdefULL));

    require_ok(runtime.configure_reservation(2U, kConfig));

    const auto pool = runtime.inspect_pool();
    require_ok(pool);
    assert(pool->base_units == 64U);
    assert(pool->scan_limit_units == 256U);
    assert(pool->upper_units == 256U);
    assert(pool->reserved_units == 64U);
    assert(pool->remaining_units == 192U);

    const auto manager = runtime.inspect_manager_entry(3U);
    require_ok(manager);
    assert(manager->resource_key == 0U);
    assert(manager->references == 0U);
    assert(manager->preserved_0c_marker == 0xdeadbeefU);
    assert(manager->payload_marker == 0x0123456789abcdefULL);

    const auto record = runtime.inspect_record(RecordHandle{0U});
    require_ok(record);
    assert(record->length_0e == 3);
    assert(record->word_18 == 0x22U);
    assert(record->signed_1c == -1);
    assert(record->length_26 == 5);
    assert(record->word_44 == 0x33U);

    // Occupancy is preserved by configure. With cell zero still occupied, the
    // automatic path advances from start index 2 to index 3 before placement.
    require_ok(runtime.seed_record_for_inspection(
        RecordHandle{1U},
        RecordFields{.length_0e = 1, .signed_1c = -1}));
    const auto placed = runtime.place_record(RecordHandle{1U}, 0, 1, kConfig);
    require_ok(placed);
    assert(*placed == PlacementStatus::placed);
    const auto placed_record = runtime.inspect_record(RecordHandle{1U});
    require_ok(placed_record);
    assert(placed_record->units_06 == 96U);

    // Unsigned blocks << 5 is intentionally wrap32. 0x08000001 << 5 becomes
    // 0x20, rather than saturating or widening the runtime behavior.
    require_ok(runtime.configure_reservation(0x08000001U, kConfig));
    const auto wrapped = runtime.inspect_pool();
    require_ok(wrapped);
    assert(wrapped->reserved_units == 32U);
    assert(wrapped->scan_limit_units == 288U);
    assert(wrapped->upper_units == 288U);
    assert(wrapped->remaining_units == 224U);
}

void test_automatic_and_palette_mode_zero() {
    PtxRuntimeCompat runtime;
    require_ok(runtime.initialize(kConfig));
    require_ok(runtime.configure_reservation(2U, kConfig));

    require_ok(runtime.seed_record_for_inspection(
        RecordHandle{0U},
        RecordFields{.length_0e = 3, .signed_1c = -1}));
    const auto normal = runtime.place_record(RecordHandle{0U}, 0, 1, kConfig);
    require_ok(normal);
    assert(*normal == PlacementStatus::placed);

    const auto normal_record = runtime.inspect_record(RecordHandle{0U});
    require_ok(normal_record);
    assert(normal_record->units_06 == 64U);
    assert(normal_record->units_1e == 160U);
    assert(normal_record->length_0e == 3);

    require_ok(runtime.seed_record_for_inspection(
        RecordHandle{1U},
        RecordFields{
            .length_0e = 9,
            .word_18 = 0U,
            .signed_1c = 0,
            .length_26 = 7,
            .word_44 = 0U,
        }));
    const auto palette = runtime.place_record(RecordHandle{1U}, 0, 0, kConfig);
    require_ok(palette);
    assert(*palette == PlacementStatus::placed);

    const auto palette_record = runtime.inspect_record(RecordHandle{1U});
    require_ok(palette_record);
    assert(palette_record->length_0e == 1);
    assert(palette_record->length_26 == 1);
    assert(palette_record->units_06 == 64U);
    assert(palette_record->units_1e == 96U);
}

void test_fragmented_failure() {
    PtxRuntimeCompat runtime;
    require_ok(runtime.initialize(kConfig));
    require_ok(runtime.configure_reservation(2U, kConfig));
    require_ok(runtime.seed_record_for_inspection(
        RecordHandle{0U},
        RecordFields{.length_0e = 1, .signed_1c = -1}));

    for (std::uint32_t cell = 0U; cell < 6U; ++cell) {
        require_ok(runtime.set_allocation_cell_for_inspection(cell, true));
    }

    const auto result = runtime.place_record(RecordHandle{0U}, 0, 1, kConfig);
    require_ok(result);
    assert(*result == PlacementStatus::rejected);
}

void test_explicit_index_canonical_oddity() {
    PtxRuntimeCompat runtime;
    require_ok(runtime.initialize(kConfig));
    require_ok(runtime.configure_reservation(2U, kConfig));
    require_ok(runtime.seed_record_for_inspection(
        RecordHandle{0U},
        RecordFields{.length_0e = 1, .signed_1c = -1}));

    // start=2, configured limit=8. The recovered EXE path intentionally skips
    // the limit read whenever requested_index >= start. Therefore 9 is accepted
    // when the represented occupancy byte is free. Do not "fix" this into a
    // conventional range check.
    const auto result = runtime.place_record(RecordHandle{0U}, 9, 1, kConfig);
    require_ok(result);
    assert(*result == PlacementStatus::placed);

    const auto record = runtime.inspect_record(RecordHandle{0U});
    require_ok(record);
    assert(record->units_06 == 352U);
    assert(record->units_1e == 384U);
}

void test_host_errors_and_pool_prefix_boundary() {
    PtxRuntimeCompat runtime;

    const auto before_init = runtime.inspect_pool();
    assert(!before_init.has_value());
    assert(before_init.error() == RuntimeError::not_initialized);

    require_ok(runtime.initialize(kConfig));
    require_ok(runtime.configure_reservation(2U, kConfig));

    const auto invalid_record = runtime.inspect_record(RecordHandle{128U});
    assert(!invalid_record.has_value());
    assert(invalid_record.error() == RuntimeError::invalid_record);

    require_ok(runtime.seed_record_for_inspection(
        RecordHandle{0U},
        RecordFields{.length_0e = 0, .signed_1c = -1}));
    const auto invalid_length = runtime.place_record(RecordHandle{0U}, 0, 1, kConfig);
    assert(!invalid_length.has_value());
    assert(invalid_length.error() == RuntimeError::invalid_length);

    require_ok(runtime.seed_record_for_inspection(
        RecordHandle{0U},
        RecordFields{.length_0e = 1, .signed_1c = -1}));

    // 0xCB48 is the first byte beyond PtxPoolImage. The initializer owns an
    // additional 8-byte storage tail up to 0xCB50, but placement must never scan
    // that tail. requested_index below maps exactly to 0xCB48.
    constexpr std::int32_t kFirstTailCell = 0xcb48 - 0x2800;
    const auto tail_scan = runtime.place_record(
        RecordHandle{0U}, kFirstTailCell, 1, kConfig);
    assert(!tail_scan.has_value());
    assert(tail_scan.error() == RuntimeError::memory_out_of_range);
}

}  // namespace

int main() {
    test_initialize_and_manager_independence();
    test_reservation_and_preservation();
    test_automatic_and_palette_mode_zero();
    test_fragmented_failure();
    test_explicit_index_canonical_oddity();
    test_host_errors_and_pool_prefix_boundary();
    return 0;
}
