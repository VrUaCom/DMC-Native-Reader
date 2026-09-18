#pragma once

#include <concepts>
#include <span>
#include <string_view>
#include <type_traits>

#include "dmcresource/cpp23_profile.h"
#include "dmcresource/spider/crusader.h"

namespace dmcresource::spider::cpp23 {

// Spider C++ is an embedded C++23 product-language profile over Reader-owned
// Crusader. It adds typed result/concept contracts for Native Reader modules;
// canonical Rengine remains read-only format/parser authority, not the product
// orchestration executor.
inline constexpr std::string_view kLanguageProfile = "spider.cpp23";
inline constexpr std::string_view kExecutorProfile = "spider.crusader";

template <class E>
concept ErrorCode = std::is_enum_v<E>;

template <class State>
concept StateObject = std::is_object_v<State> && !std::is_pointer_v<State>;

template <class T, ErrorCode E>
using Result = dmcresource::cpp23::Result<T, E>;

template <ErrorCode E>
using Status = dmcresource::cpp23::Status<E>;

template <StateObject State>
[[nodiscard]] inline crusader::ExecutionReport execute(
    const crusader::Plan& plan,
    std::span<const crusader::OperationBinding> bindings,
    State& state) noexcept {
    return crusader::execute(plan, bindings, &state);
}

}  // namespace dmcresource::spider::cpp23
