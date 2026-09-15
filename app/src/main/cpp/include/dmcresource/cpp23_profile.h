#pragma once

#include <expected>
#include <string_view>
#include <version>

#if __cplusplus < 202302L
#error "DMC Native Reader product core requires C++23"
#endif

#if !defined(__cpp_lib_expected) || __cpp_lib_expected < 202202L
#error "DMC Native Reader C++23 profile requires std::expected >= 202202L"
#endif

namespace dmcresource::cpp23 {

inline constexpr std::string_view kProfile = "dmc.native-reader.cpp23";
inline constexpr long kLanguageLevel = __cplusplus;
inline constexpr long kExpectedFeature = __cpp_lib_expected;

template <class T, class E>
using Result = std::expected<T, E>;

template <class E>
using Status = std::expected<void, E>;

}  // namespace dmcresource::cpp23
