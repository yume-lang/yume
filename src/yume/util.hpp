#pragma once

#include "diagnostic/source_location.hpp"
#include <array>
#include <cstddef>
#include <experimental/memory>
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
#include <utility>
#include <variant>
#include <vector>

namespace yume {

using namespace std::literals::string_literals;
using namespace std::literals::string_view_literals;
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
using std::experimental::observer_ptr;

using llvm::cast;
using llvm::dyn_cast;
using llvm::errs;
using llvm::isa;
using std::move;

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
  if constexpr (ENABLE_ASSERT)
    if (!assertion) {
      llvm::errs() << "*** assertion failed: " << at(location) << " " << log_msg << '\n';
      std::abort();
    }
}

/// Opens a writeable stream to a file with the given filename relative to the current working directory.
auto inline open_file(const char* filename) -> unique_ptr<llvm::raw_pwrite_stream> {
  std::error_code error_code;
  auto dest =
      std::make_unique<llvm::raw_fd_ostream>(filename, error_code, llvm::sys::fs::CreationDisposition::CD_CreateAlways);

  if (error_code) {
    llvm::errs() << "Could not open file: " << error_code.message() << "\n";
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
