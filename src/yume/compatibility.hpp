#pragma once

#include "util.hpp"
#include <cstdint>
#include <sstream>

namespace yume::ty {
class Generic;
class Type;

struct Conv {
  enum struct Kind : uint8_t { None, Int };

  using Kind::Int;
  using Kind::None;

  bool dereference = false;
  bool generic = false;
  Kind kind{};

  [[nodiscard]] constexpr auto empty() const -> bool { return !dereference && !generic && kind == None; }

  [[nodiscard]] auto to_string() const -> string {
    if (empty())
      return "noconv";

    std::stringstream ss;
    if (generic)
      ss << "generic ";
    if (dereference)
      ss << "deref ";
    if (kind == Int)
      ss << "int ";
    ss << "conv";
    return ss.str();
  }
};

/// The compatibility between two types, for overload selection.
struct Compat {
  bool valid = false;
  bool indirection = false;
  Conv conv{};
  const Generic* substituted_generic{};
  const Type* substituted_with{};
};
} // namespace yume::ty
