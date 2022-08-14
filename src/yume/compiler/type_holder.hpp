#pragma once

#include "ty/type.hpp"
#include <array>
#include <llvm/ADT/StringMap.h>
#include <memory>
#include <vector>

namespace yume {
class Compiler;
struct TypeHolder {
  struct IntTypePair {
    ty::Int* s_ty;
    ty::Int* u_ty;
  };

  array<IntTypePair, 4> int_types{};
  ty::Int* bool_type{};
  IntTypePair size_type{};
  llvm::StringMap<unique_ptr<ty::BaseType>> known{};
  std::vector<unique_ptr<ty::BaseType>> template_instantiations{};

  TypeHolder();

  void declare_size_type(Compiler&);

  constexpr auto int8() -> IntTypePair { return int_types[0]; }
  constexpr auto int16() -> IntTypePair { return int_types[1]; }
  constexpr auto int32() -> IntTypePair { return int_types[2]; }
  constexpr auto int64() -> IntTypePair { return int_types[3]; }
};
} // namespace yume
