#include "ast.hpp"

#include "qualifier.hpp"
#include "util.hpp"
#include <llvm/ADT/STLExtras.h>
#include <sstream>
#include <string>

namespace yume::ast {

auto AST::describe() const -> string { return string{"unknown "} + kind_name(); }
auto QualType::describe() const -> string {
  switch (qualifier) {
  case Qualifier::Ptr: return "ptr";
  case Qualifier::Mut: return "mut";
  case Qualifier::Ref: return "ref";
  default: return "";
  }
}
auto TemplatedType::describe() const -> string {
  stringstream ss{};
  ss << "{";
  for (const auto& i : llvm::enumerate(type_args)) {
    if (i.index() > 0)
      ss << ",";
    ss << i.value().ast()->describe();
  }
  ss << "}";
  return ss.str();
}
auto FunctionType::describe() const -> string {
  stringstream ss{};
  ss << "->(";
  for (const auto& i : llvm::enumerate(args)) {
    if (i.index() > 0)
      ss << ",";
    ss << i.value()->describe();
  }
  ss << ")";
  if (ret.has_value())
    ss << ret->describe();
  return ss.str();
}
} // namespace yume::ast
