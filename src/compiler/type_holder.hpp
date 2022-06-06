#pragma once

#include "../type.hpp"
#include "../util.hpp"
#include <array>
#include <llvm/ADT/StringMap.h>
#include <map>
#include <memory>
#include <string>

namespace yume {
struct TypeHolder {
  struct IntTypePair {
    ty::Int* s_ty;
    ty::Int* u_ty;
  };

  std::array<IntTypePair, 4> int_types{};
  ty::UnknownType unknown{};
  llvm::StringMap<unique_ptr<ty::Type>> known{};

  TypeHolder();

  inline constexpr auto int8() -> IntTypePair { return int_types[0]; }
  inline constexpr auto int16() -> IntTypePair { return int_types[1]; }
  inline constexpr auto int32() -> IntTypePair { return int_types[2]; }
  inline constexpr auto int64() -> IntTypePair { return int_types[3]; }
};
} // namespace yume
