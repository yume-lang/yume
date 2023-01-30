#pragma once

#include "diagnostic/source_location.hpp"
#include <array>
#include <cstddef>
#include <filesystem>
#include <llvm/Support/Casting.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/raw_ostream.h>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <sstream>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace yume {
using namespace std::literals::string_literals;
using namespace std::literals::string_view_literals;
namespace fs = std::filesystem;
using std::array;
using std::optional;
using std::span;
using std::string;
using std::string_view;
using std::stringstream;
using std::tuple;
using std::unique_ptr;
using std::variant;
using std::vector;

using llvm::cast;
using llvm::dyn_cast;
using llvm::errs;
using llvm::isa;
using std::move;

template <typename... Ts>
struct visitable_variant : public std::variant<Ts...> { // NOLINT(readability-identifier-naming): STL-like class
private:
  template <typename... Vs> struct Visitor : Vs... {
    using Vs::operator()...;
  };
  template <typename... Vs> Visitor(Vs...) -> Visitor<Vs...>;

public:
  using std::variant<Ts...>::variant;

  template <typename... Us> auto visit(Us... us) -> decltype(auto) { return std::visit(Visitor<Us...>{us...}, *this); }
  template <typename... Us> [[nodiscard]] auto visit(Us... us) const -> decltype(auto) {
    return std::visit(Visitor<Us...>{us...}, *this);
  }
};

template <typename T>
concept visitable = requires(T t) {
                      {
                        t.visit([](auto&&) {})
                      };
                    };

#if __has_feature(nullability)
template <typename T> using nullable = T _Nullable;
template <typename T> using nonnull = T _Nonnull;
#else
template <typename T> using nullable = T;
template <typename T> using nonnull = T;
#endif

#ifdef NDEBUG
constexpr bool ENABLE_ASSERT = false;
#else
constexpr bool ENABLE_ASSERT = true;
#endif

template <typename T>
constexpr inline void yume_assert(T&& assertion, const std::string_view log_msg = {},
                                  const source_location location = source_location::current()) noexcept {
  if constexpr (ENABLE_ASSERT) {
    if (!assertion) {
      llvm::errs() << "*** assertion failed: " << at(location) << " " << log_msg << '\n';
      std::abort();
    }
  }
}

/// Opens a writeable stream to a file with the given filename relative to the current working directory.
auto inline open_file(nonnull<const char*> filename) -> unique_ptr<llvm::raw_pwrite_stream> {
  std::error_code error_code;
  auto dest =
      std::make_unique<llvm::raw_fd_ostream>(filename, error_code, llvm::sys::fs::CreationDisposition::CD_CreateAlways);

  if (error_code) {
    llvm::errs() << "Could not open file: " << error_code.message() << "\n";
    throw;
  }

  return dest;
}

template <size_t N> struct StringLiteral {
  // NOLINTNEXTLINE(modernize-avoid-c-arrays,cppcoreguidelines-avoid-c-arrays)
  constexpr StringLiteral(const char (&str)[N]) { std::copy_n(str, N, value); }

  char value[N]{}; // NOLINT(modernize-avoid-c-arrays,cppcoreguidelines-avoid-c-arrays)
  auto operator<=>(const StringLiteral&) const = default;
};

template <typename T> static void hash_combine(uint64_t& seed, const T& v) {
  static constexpr auto PHI_FRAC = std::numbers::phi_v<long double> - 1;
  static constexpr auto ALL_BITS = std::numeric_limits<std::size_t>::max();
  static constexpr auto FLOATING_HASH_CONST = PHI_FRAC * ALL_BITS;
  static constexpr auto HASH_CONST = static_cast<std::size_t>(FLOATING_HASH_CONST);
  static constexpr auto TWIST_LEFT = 6;
  static constexpr auto TWIST_RIGHT = 2;

  std::hash<T> hasher;
  seed ^= hasher(v) + HASH_CONST + (seed << TWIST_LEFT) + (seed >> TWIST_RIGHT);
}

} // namespace yume
