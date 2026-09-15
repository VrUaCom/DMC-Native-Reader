#pragma once

#include <expected>
#include <string_view>
#include <version>

#if __cplusplus < 202302L
#error "DMC Native Reader product core requires C++23"
#endif

#ifndef __cpp_lib_expected
#error "DMC Native Reader C++23 profile requires std::expected support"
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
