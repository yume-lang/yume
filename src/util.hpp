//
// Created by rymiel on 5/8/22.
//

#ifndef YUME_CPP_UTIL_HPP
#define YUME_CPP_UTIL_HPP

#include <iostream>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/raw_ostream.h>
#include <memory>
#include <set>
#include <utility>
#include <vector>

namespace yume {

template <typename T> using vector = std::vector<T>;

template <typename T> using unique_ptr = std::unique_ptr<T>;

using string = std::string;

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

  constexpr operator std::string() const { // NOLINT(google-explicit-constructor)
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

#endif // YUME_CPP_UTIL_HPP
