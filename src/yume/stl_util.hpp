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

/// \brief A view over a range of pointer-like types, where every element is dereferenced
///
/// Creating a dereference view from a type such as `vector<unique_ptr<T>>` will give a range-like view, where each
/// element is a const T&
template <std::ranges::input_range T>
requires pointer_like<typename std::iterator_traits<std::ranges::iterator_t<T>>::value_type>
class dereference_view { // NOLINT(readability-identifier-naming): STL-like class
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
    using reference =
        decltype(*std::declval<typename std::iterator_traits<std::ranges::iterator_t<Base>>::value_type>());
    using value_type = std::remove_reference_t<reference>;

    Iterator() requires std::default_initializable<Base_iter>
    = default;

    constexpr Iterator(const Parent* parent, Base_iter current) : m_current(move(current)), m_parent(parent) {}

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

  [[nodiscard]] constexpr auto begin() const -> Iterator { return Iterator{this, std::ranges::begin(m_base)}; }

  constexpr auto size() -> size_t { return std::ranges::size(m_base); }

  constexpr auto operator[](typename Iterator::difference_type n)
      -> decltype(auto) requires std::ranges::random_access_range<typename Iterator::Base> {
    return begin()[n];
  }

  [[nodiscard]] constexpr auto end() const -> Iterator { return Iterator{this, std::ranges::end(m_base)}; }
};

namespace {
using dview = dereference_view<std::vector<std::unique_ptr<int>>>;
static_assert(std::ranges::range<dview>);
static_assert(std::ranges::input_range<dview>);
static_assert(std::is_same_v<decltype(std::declval<dview>().begin())::value_type, int>);
} // namespace

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
