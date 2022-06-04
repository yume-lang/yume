//
// Created by rymiel on 5/8/22.
//

#ifndef YUME_CPP_UTIL_HPP
#define YUME_CPP_UTIL_HPP

#include <cstddef>
#include <iostream>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/raw_ostream.h>
#include <memory>
#include <optional>
#include <ranges>
#include <set>
#include <span>
#include <utility>
#include <variant>
#include <vector>

namespace yume {

using std::optional;
using std::span;
using std::string;
using std::unique_ptr;
using std::variant;
using std::vector;

template <class T>
concept pointer_like = requires(T t) {
  {*t};
};

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
    Parent* m_parent = nullptr;

  public:
    using difference_type = std::ranges::range_difference_t<Base>;

    Iterator() requires std::default_initializable<Base_iter>
    = default;

    constexpr Iterator(Parent* parent, Base_iter current) : m_current(std::move(current)), m_parent(parent) {}

    constexpr auto operator*() const -> decltype(auto) { return **m_current; }

    constexpr auto operator->() const -> decltype(auto) { return *m_current; }

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

  constexpr auto begin() -> Iterator { return Iterator{this, std::ranges::begin(m_base)}; }

  constexpr auto size() -> size_t { return std::ranges::size(m_base); }

  constexpr auto operator[](typename Iterator::difference_type n)
      -> decltype(auto) requires std::ranges::random_access_range<typename Iterator::Base> {
    return begin()[n];
  }

  constexpr auto end() -> Iterator { return Iterator{this, std::ranges::end(m_base)}; }
};

template <typename T>
requires pointer_like<T>
auto inline constexpr try_dereference(const std::optional<T>& opt) {
  using U = std::reference_wrapper<std::remove_reference_t<decltype(*opt.value())>>;
  if (opt.has_value()) {
    return std::optional<U>(*opt.value());
  }
  return std::optional<U>{};
}

auto inline open_file(const char* filename) -> unique_ptr<llvm::raw_pwrite_stream> {
  std::error_code errorCode;
  auto dest = std::make_unique<llvm::raw_fd_ostream>(filename, errorCode, llvm::sys::fs::OF_None);

  if (errorCode) {
    llvm::errs() << "Could not open file: " << errorCode.message() << "\n";
    throw;
  }

  return dest;
}

struct Atom {
  const string* m_str;

  constexpr Atom() = delete;
  explicit Atom(const string* str) : m_str{str} {}

  operator std::string() const { // NOLINT(google-explicit-constructor)
    return *m_str;
  }
  auto inline operator<=>(const Atom& other) const noexcept = default;

  static auto inline make_atom(const string& value) noexcept -> Atom {
    auto data = Atom::interned.emplace(value).first;
    return Atom{&*data};
  }

private:
  static inline std::set<string> interned; // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)
};

// using Atom = const char*;

auto inline make_atom(const string& value) noexcept -> Atom { return Atom::make_atom(value); }

namespace atom_literal {
auto inline operator""_a(const char* value, std::size_t len) noexcept -> Atom { return make_atom(string(value, len)); }
} // namespace atom_literal
} // namespace yume

using namespace yume::atom_literal;
using namespace std::literals::string_literals;

#endif // YUME_CPP_UTIL_HPP
