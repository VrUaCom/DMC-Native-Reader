#include <cassert>
#include <cstdint>
#include <string_view>
#include <utility>

#include "dmcresource/cpp23_profile.h"
#include "dmcresource/native_module.h"
#include "dmcresource/spider/cpp23_language.h"
#include "dmcresource/texture_companion.h"

namespace {

enum class TestError : std::uint8_t {
    Rejected = 7U,
};

struct TestState {
    int value{};
};

static_assert(dmcresource::cpp23::kLanguageLevel > 202002L);
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

// C++23 migration exception contract: allocating helpers are allowed to throw,
// while the portable pipeline and NativeModule ABI remain explicit fail-closed
// noexcept boundaries.
static_assert(!noexcept(dmcresource::probe({}, nullptr, 0U)));
static_assert(!noexcept(dmcresource::NativeModuleRegistry::modules()));
static_assert(noexcept(dmcresource::run_decode_pipeline({}, nullptr, 0U)));
static_assert(noexcept(std::declval<dmcresource::ModuleRun>()(
    std::declval<const dmcresource::NativeModule&>(),
    std::string_view{},
    nullptr,
    0U,
    std::declval<const dmcresource::ProbeResult&>())));

// Texture-companion attachment builds diagnostics and decoded banks, so it is
// intentionally throwing-capable below the Spider OperationFn catch boundary.
// The pure capability gate remains a fail-closed noexcept query.
static_assert(noexcept(dmcresource::texture_companion::can_attach(
    std::declval<const dmcresource::texture_companion::ModelTextureView&>())));
static_assert(!noexcept(dmcresource::texture_companion::attach_ptx(
    std::string_view{},
    nullptr,
    0U,
    std::declval<const dmcresource::texture_companion::ModelTextureView&>())));
static_assert(!noexcept(dmcresource::texture_companion::attach_shared_ptx(
    std::string_view{},
    nullptr,
    0U,
    std::span<const dmcresource::texture_companion::ModelTextureView>{})));

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
