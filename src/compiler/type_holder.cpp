#include "type_holder.hpp"
#include "../type.hpp"
#include "../util.hpp"
#include <initializer_list>
#include <utility>

namespace yume {
TypeHolder::TypeHolder() {
  int j = 0;
  for (int i : {8, 16, 32, 64}) {
    IntTypePair ints{};
    for (bool is_signed : {true, false}) {
      string type_name = (is_signed ? "I"s : "U"s) + std::to_string(i);
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
} // namespace yume
