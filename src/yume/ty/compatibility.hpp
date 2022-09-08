#pragma once

#include "util.hpp"
#include <cstdint>
#include <sstream>

namespace yume::ty {
class Generic;
class BaseType;

struct Conv {
  enum struct Kind : uint8_t { None, Int, FnPtr };

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
    else if (kind == Kind::FnPtr)
      ss << "fnptr ";
    return ss.str();
  }
};

/// The compatibility between two types, for overload selection.
struct Compat {
  bool valid = false;
  Conv conv{};
};
} // namespace yume::ty
