#include "extra/mangle.hpp"

namespace yume::mangle {

static auto length_encode(const string& name) -> string { return std::to_string(name.length()) + name; }

auto mangle_name(Fn& fn) -> string {
  stringstream ss{};
  ss << "_Ym.";
  ss << fn.ast().name();
  ss << "(";
  for (const auto& i : llvm::enumerate(fn.arg_types())) {
    if (i.index() > 0)
      ss << ",";
    ss << mangle_name(i.value(), &fn);
  }
  ss << ")";
  // TODO(rymiel): should mangled names even contain the return type...?
  if (auto ret = fn.ret(); ret.has_value())
    ss << mangle_name(*ret, &fn);

  return ss.str();
}

auto mangle_name(ty::Type ast_type, DeclLike parent) -> string {
  stringstream ss{};
  if (const auto* ptr_type = ast_type.base_dyn_cast<ty::Ptr>()) {
    ss << mangle_name(ptr_type->pointee(), parent);
    if (ptr_type->has_qualifier(Qualifier::Ptr))
      ss << "*";
  } else if (const auto* generic_type = ast_type.base_dyn_cast<ty::Generic>()) {
    auto match = parent.subs()->find(generic_type->name());
    yume_assert(match != parent.subs()->end(), "Cannot mangle unsubstituted generic");
    ss << match->second.name();
  } else {
    ss << ast_type.base_name();
  }

  if (ast_type.is_mut())
    ss << "&";

  return ss.str();
}

} // namespace yume::mangle
