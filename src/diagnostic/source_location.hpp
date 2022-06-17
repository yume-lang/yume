#pragma once

#include <string>

#if __has_include(<source_location>) && __has_builtin(__builtin_source_location)
#include <source_location>
#define yume_has_source_location 1
#elif __has_include(<experimental/source_location>)
#include <experimental/source_location>
#define yume_has_source_location -1
#else
#define yume_has_source_location 0
#endif

namespace yume {
#if yume_has_source_location == 1
using std::source_location;
#elif yume_has_source_location == -1
using std::experimental::source_location;
#else
struct source_location {
  constexpr inline auto file_name() const { return "??"; }     // NOLINT
  constexpr inline auto function_name() const { return "??"; } // NOLINT
  constexpr inline auto line() const { return -1; }            // NOLINT
  constexpr inline auto column() const { return -1; }          // NOLINT
  static inline constexpr auto current() { return source_location{}; }
};
#endif

inline auto at(const source_location location = source_location::current()) -> std::string {
  return std::string(location.file_name()) + ":" + std::to_string(location.line()) + ":" +
         std::to_string(location.column()) + " in " + location.function_name();
}
} // namespace yume
