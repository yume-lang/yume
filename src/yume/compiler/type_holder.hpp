#pragma once

#include "type.hpp"
#include <array>
#include <llvm/ADT/StringMap.h>
#include <memory>

namespace yume {
struct TypeHolder {
  struct IntTypePair {
    ty::Int* s_ty;
    ty::Int* u_ty;
  };

  std::array<IntTypePair, 4> int_types{};
  ty::Int* bool_type{};
  llvm::StringMap<unique_ptr<ty::Type>> known{};

  TypeHolder();

  constexpr auto int8() -> IntTypePair { return int_types[0]; }
  constexpr auto int16() -> IntTypePair { return int_types[1]; }
  constexpr auto int32() -> IntTypePair { return int_types[2]; }
  constexpr auto int64() -> IntTypePair { return int_types[3]; }
};
} // namespace yume
