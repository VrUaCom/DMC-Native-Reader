#include <cassert>
#include <string_view>

#include "dmcresource/cpp23_profile.h"
#include "dmcresource/spider/cpp23_language.h"

namespace {

enum class TestError {
    Rejected,
};

struct TestState {
    int value{};
};

static_assert(__cplusplus >= 202302L);
static_assert(dmcresource::cpp23::kProfile == "dmc.native-reader.cpp23");
static_assert(dmcresource::spider::cpp23::kLanguageProfile == "spider.cpp23");
static_assert(dmcresource::spider::cpp23::ErrorCode<TestError>);
static_assert(dmcresource::spider::cpp23::StateObject<TestState>);
static_assert(!dmcresource::spider::cpp23::StateObject<TestState*>);

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
