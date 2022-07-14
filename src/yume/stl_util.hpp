#pragma once

#include <memory>
#include <ranges>
#include <type_traits>
#include <vector>

namespace yume {

template <class T>
concept pointer_like = requires(T t) {
  {*t};
};

/// Dereference an `optional` holding a pointer-like value, if it is holding a value
/**
 * Because C++20 doesn't have monadic operations for optionals yet, which should be a basic, fundamental feature of an
 * "Optional" type
 */
template <typename T>
requires pointer_like<T>
auto inline constexpr try_dereference(const std::optional<T>& opt) {
  using U = std::reference_wrapper<std::remove_reference_t<decltype(*opt.value())>>;
  if (opt.has_value())
    return std::optional<U>(*opt.value());
  return std::optional<U>{};
}
} // namespace yume
