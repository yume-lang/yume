#pragma once

#include "util.hpp"
#include <cstdint>
#include <sstream>

namespace yume::ty {
class Generic;
class Type;

enum struct ConversionKind : uint8_t { None, Int };

struct Conversion {
  bool dereference = false;
  bool generic = false;
  ConversionKind kind{};

  [[nodiscard]] constexpr auto empty() const -> bool {
    return !dereference && !generic && kind == ConversionKind::None;
  }

  [[nodiscard]] auto to_string() const -> string {
    if (empty())
      return "no conversion";

    std::stringstream ss;
    if (generic)
      ss << "generic ";
    if (dereference)
      ss << "deref ";
    if (kind == ConversionKind::Int)
      ss << "integer ";
    ss << "conversion";
    return ss.str();
  }
};

/// The compatibility between two types, for overload selection.
struct Compatiblity {
  bool valid = false;
  bool indirection = false;
  Conversion conversion{};
  const Generic* substituted_generic{};
  const Type* substituted_with{};
};
} // namespace yume::ty
