#include "extra/mangle.hpp"

namespace yume::mangle {

[[maybe_unused]] static auto length_encode(const string& name) -> string {
  return std::to_string(name.length()) + name;
}

auto mangle_name(Fn& fn) -> string {
  // TODO(rymiel): static function declarations (i.e. without self) in multiple structs will have identical names.
  // thus, the recevier should probably be included in the mangled name
  stringstream ss{};
  ss << "_Ym.";
  ss << fn.name();
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
    auto match = parent.subs()->find_type(generic_type->name());
    YUME_ASSERT(match.has_value(), "Cannot mangle unsubstituted generic");
    ss << match->name();
  } else {
    ss << ast_type.base_name();
  }

  if (ast_type.is_mut())
    ss << "&";

  if (ast_type.is_ref())
    ss << "^";

  if (ast_type.is_meta())
    ss << "+";

  return ss.str();
}

} // namespace yume::mangle
