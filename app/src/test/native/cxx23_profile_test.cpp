#include <cassert>
#include <cstdint>
#include <string_view>

#include "dmcresource/cpp23_profile.h"
#include "dmcresource/spider/cpp23_language.h"

namespace {

enum class TestError : std::uint8_t {
    Rejected = 7U,
};

struct TestState {
    int value{};
};

// CMake selects strict C++23; the profile rejects C++20-or-older and verifies
// concrete C++23 library facilities instead of assuming every compiler reports
// exactly 202302L in __cplusplus.
static_assert(__cplusplus > 202002L);
static_assert(dmcresource::cpp23::kExpectedFeature >= 202202L);
static_assert(dmcresource::cpp23::kByteswapFeature >= 202110L);
static_assert(dmcresource::cpp23::kToUnderlyingFeature >= 202102L);
static_assert(dmcresource::cpp23::kProfile == "dmc.native-reader.cpp23");
static_assert(dmcresource::spider::cpp23::kLanguageProfile == "spider.cpp23");
static_assert(dmcresource::spider::cpp23::ErrorCode<TestError>);
static_assert(dmcresource::spider::cpp23::StateObject<TestState>);
static_assert(!dmcresource::spider::cpp23::StateObject<TestState*>);
static_assert(std::byteswap<std::uint32_t>(0x11223344U) == 0x44332211U);
static_assert(std::to_underlying(TestError::Rejected) == 7U);

}  // namespace

int main() {
    dmcresource::cpp23::Result<int, TestError> value = 42;
    assert(value.has_value());
    assert(*value == 42);

    dmcresource::spider::cpp23::Result<int, TestError> rejected =
        std::unexpected(TestError::Rejected);
    assert(!rejected.has_value());
    assert(rejected.error() == TestError::Rejected);

    dmcresource::spider::cpp23::Status<TestError> ok;
    assert(ok.has_value());
    return 0;
}
