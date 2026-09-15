#pragma once

#include <bit>
#include <expected>
#include <string_view>
#include <utility>
#include <version>

#if __cplusplus < 202302L
#error "DMC Native Reader product core requires C++23"
#endif

#if !defined(__cpp_lib_expected) || __cpp_lib_expected < 202202L
#error "DMC Native Reader C++23 profile requires std::expected >= 202202L"
#endif

#if !defined(__cpp_lib_byteswap) || __cpp_lib_byteswap < 202110L
#error "DMC Native Reader C++23 profile requires std::byteswap"
#endif

#if !defined(__cpp_lib_to_underlying) || __cpp_lib_to_underlying < 202102L
#error "DMC Native Reader C++23 profile requires std::to_underlying"
#endif

namespace dmcresource::cpp23 {

inline constexpr std::string_view kProfile = "dmc.native-reader.cpp23";
inline constexpr long kLanguageLevel = __cplusplus;
inline constexpr long kExpectedFeature = __cpp_lib_expected;
inline constexpr long kByteswapFeature = __cpp_lib_byteswap;
inline constexpr long kToUnderlyingFeature = __cpp_lib_to_underlying;

template <class T, class E>
using Result = std::expected<T, E>;

template <class E>
using Status = std::expected<void, E>;

}  // namespace dmcresource::cpp23
