#pragma once

#include "diagnostic/source_location.hpp"
#include <cstddef>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/raw_ostream.h>
#include <memory>
#include <optional>
#include <ranges>
#include <set>
#include <span>
#include <sstream>
#include <utility>
#include <variant>
#include <vector>

namespace yume {

using namespace std::literals::string_literals;
using std::optional;
using std::span;
using std::string;
using std::stringstream;
using std::unique_ptr;
using std::variant;
using std::vector;

#ifdef NDEBUG
constexpr bool ENABLE_ASSERT = false;
#else
constexpr bool ENABLE_ASSERT = true;
#endif

template <typename T>
constexpr inline void yume_assert(T&& assertion, const std::string_view log_msg = {},
                                  const source_location location = source_location::current()) noexcept {
  if constexpr (ENABLE_ASSERT)
    if (!assertion) {
      llvm::errs() << "*** assertion failed: " << at(location) << " " << log_msg << '\n';
      std::abort();
    }
}

template <class T>
concept pointer_like = requires(T t) {
  {*t};
};

/// \brief A view over a range of pointer-like types, where every element is dereferenced
///
/// Creating a dereference view from a type such as `vector<unique_ptr<T>>` will give a range-like view, where each
/// element is a const T&
template <std::ranges::input_range T>
requires pointer_like<typename std::iterator_traits<std::ranges::iterator_t<T>>::value_type>
class dereference_view {
private:
  using Base = T;

  const T& m_base;

  struct Iterator {
    friend dereference_view;

  private:
    using Parent = dereference_view;
    using Base = dereference_view::Base;
    using Base_iter = std::ranges::iterator_t<std::add_const_t<Base>>;

    Base_iter m_current = Base_iter();
    const Parent* m_parent = nullptr;

  public:
    using difference_type = std::ranges::range_difference_t<Base>;

    Iterator() requires std::default_initializable<Base_iter>
    = default;

    constexpr Iterator(const Parent* parent, Base_iter current) : m_current(std::move(current)), m_parent(parent) {}

    constexpr auto operator*() const -> decltype(auto) { return **m_current; }

    constexpr auto operator*() -> decltype(auto) { return **m_current; }

    constexpr auto operator->() const -> decltype(auto) { return *m_current; }

    constexpr auto operator->() -> decltype(auto) { return *m_current; }

    constexpr auto operator++() -> Iterator& {
      ++m_current;
      return *this;
    }

    constexpr void operator++(int) { ++m_current; }

    constexpr auto operator++(int) -> Iterator requires std::ranges::forward_range<Base> {
      auto tmp = *this;
      ++*this;
      return tmp;
    }

    constexpr auto operator--() -> Iterator& requires std::ranges::bidirectional_range<Base> {
      --m_current;
      return *this;
    }

    constexpr auto operator--(int) -> Iterator requires std::ranges::bidirectional_range<Base> {
      auto tmp = *this;
      --*this;
      return tmp;
    }

    constexpr auto operator+=(difference_type n) -> Iterator& requires std::ranges::random_access_range<Base> {
      m_current += n;
      return *this;
    }

    constexpr auto operator-=(difference_type n) -> Iterator& requires std::ranges::random_access_range<Base> {
      m_current -= n;
      return *this;
    }

    constexpr auto operator[](difference_type n) const
        -> decltype(auto) requires std::ranges::random_access_range<Base> {
      return *m_current[n];
    }

    friend constexpr auto operator==(const Iterator& x, const Iterator& y)
        -> bool requires std::equality_comparable<Base_iter> {
      return x.m_current == y.m_current;
    }

    friend constexpr auto operator<=>(const Iterator& x, const Iterator& y) { return x.m_current <=> y.m_current; }

    friend constexpr auto operator+(Iterator i, difference_type n)
        -> Iterator requires std::ranges::random_access_range<Base> {
      return {i.m_parent, i.m_current + n};
    }

    friend constexpr auto operator+(difference_type n, Iterator i)
        -> Iterator requires std::ranges::random_access_range<Base> {
      return {i.m_parent, i.m_current + n};
    }

    friend constexpr auto operator-(Iterator i, difference_type n)
        -> Iterator requires std::ranges::random_access_range<Base> {
      return {i.m_parent, i.m_current - n};
    }

    friend constexpr auto operator-(const Iterator& x, const Iterator& y) -> difference_type {
      return x.m_current - y.m_current;
    }
  };

public:
  constexpr explicit dereference_view(const T& base) : m_base(base) {}

  constexpr auto begin() const -> Iterator { return Iterator{this, std::ranges::begin(m_base)}; }

  constexpr auto size() -> size_t { return std::ranges::size(m_base); }

  constexpr auto operator[](typename Iterator::difference_type n)
      -> decltype(auto) requires std::ranges::random_access_range<typename Iterator::Base> {
    return begin()[n];
  }

  constexpr auto end() const -> Iterator { return Iterator{this, std::ranges::end(m_base)}; }
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

/// Opens a writeable stream to a file with the given filename relative to the current working directory.
auto inline open_file(const char* filename) -> unique_ptr<llvm::raw_pwrite_stream> {
  std::error_code errorCode;
  auto dest =
      std::make_unique<llvm::raw_fd_ostream>(filename, errorCode, llvm::sys::fs::CreationDisposition::CD_CreateAlways);

  if (errorCode) {
    llvm::errs() << "Could not open file: " << errorCode.message() << "\n";
    throw;
  }

  return dest;
}

/// \brief The stem of a path-like string, which is the component after the last slash.
///
/// "foo/bar/file.txt" -> "file.txt"
[[nodiscard]] auto inline stem(std::string_view sv) -> std::string_view {
  auto delim = sv.rfind('/');
  return sv.substr(delim == string::npos ? 0 : delim + 1);
}
} // namespace yume
