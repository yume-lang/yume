#pragma once

#include <set>
#include <string>

namespace yume {
/// `Atom`s represent strings in a string pool.
/** This means two atoms created with the same string value will contain
 * identical pointers to that string. Thus, comparing `Atom`s is extremely cheap, as it only consists of a pointer
 * equality check.
 */
struct Atom {
  const std::string* m_str;

  constexpr Atom() = delete;
  explicit Atom(const std::string* str) : m_str{str} {}

  operator std::string() const { // NOLINT(google-explicit-constructor)
    return *m_str;
  }
  auto operator<=>(const Atom& other) const noexcept = default;

  static auto inline make_atom(const std::string& value) noexcept -> Atom {
    auto data = Atom::interned.emplace(value).first;
    return Atom{&*data};
  }

private:
  static inline std::set<std::string> interned; // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)
};

// using Atom = const char*;

/// Create an `Atom` with the given string content.
/// \sa atom_literal::operator""_a
auto inline make_atom(const std::string& value) noexcept -> Atom { return Atom::make_atom(value); }

auto inline operator""_a(const char* value, std::size_t len) noexcept -> Atom {
  return make_atom(std::string(value, len));
}
} // namespace yume
