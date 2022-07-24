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
  Kind kind{};

  [[nodiscard]] constexpr auto empty() const -> bool { return !dereference && kind == None; }

  [[nodiscard]] auto to_string() const -> string {
    if (empty())
      return "noconv";

    stringstream ss;
    if (dereference)
      ss << "deref ";
    if (kind == Int)
      ss << "int ";
    return ss.str();
  }
};

/// The compatibility between two types, for overload selection.
struct Compat {
  bool valid = false;
  Conv conv{};
};

struct Sub {
  const Generic* target{};
  const Type* replace{};

  auto constexpr operator<=>(const Sub& other) const noexcept = default;
};
} // namespace yume::ty
