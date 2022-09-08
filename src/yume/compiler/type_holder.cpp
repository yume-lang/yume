#include "type_holder.hpp"
#include "compiler/compiler.hpp"
#include "ty/type.hpp"
#include "util.hpp"
#include <initializer_list>
#include <llvm/ADT/StringRef.h>
#include <string>
#include <utility>

namespace yume {
TypeHolder::TypeHolder() {
  int j = 0;
  for (const int i : {8, 16, 32, 64}) {
    IntTypePair ints{};
    for (const bool is_signed : {true, false}) {
      const string type_name = (is_signed ? "I"s : "U"s) + std::to_string(i);
      auto i_ty = std::make_unique<ty::Int>(type_name, i, is_signed);
      (is_signed ? ints.s_ty : ints.u_ty) = i_ty.get();
      known.insert({i_ty->name(), move(i_ty)});
    }
    int_types.at(j++) = ints;
  }

  auto bool_ty = std::make_unique<ty::Int>("Bool", 1, false);
  bool_type = bool_ty.get();
  known.insert({bool_ty->name(), move(bool_ty)});
}

void TypeHolder::declare_size_type(Compiler& compiler) {
  for (const bool is_signed : {true, false}) {
    unsigned size_bits = compiler.ptr_bitsize();
    const string type_name = (is_signed ? "I"s : "U"s) + "Size";
    auto i_ty = std::make_unique<ty::Int>(type_name, size_bits, is_signed);
    (is_signed ? size_type.s_ty : size_type.u_ty) = i_ty.get();
    known.insert({i_ty->name(), move(i_ty)});
  }
}

auto TypeHolder::find_or_create_fn_type(const vector<ty::Type>& args, optional<ty::Type> ret,
                                        const vector<ty::Type>& closure) -> ty::Function* {
  for (const auto& i : fn_types)
    if (i->ret() == ret && i->args() == args && i->closure() == closure && !i->is_fn_ptr())
      return i.get();

  auto& new_fn = fn_types.emplace_back(std::make_unique<ty::Function>("", args, ret, closure, false));
  return new_fn.get();
}

auto TypeHolder::find_or_create_fn_ptr_type(const vector<ty::Type>& args, optional<ty::Type> ret) -> ty::Function* {
  for (const auto& i : fn_types)
    if (i->ret() == ret && i->args() == args && i->closure().empty() && i->is_fn_ptr())
      return i.get();

  auto& new_fn = fn_types.emplace_back(std::make_unique<ty::Function>("", args, ret, vector<ty::Type>{}, true));
  return new_fn.get();
}
} // namespace yume
