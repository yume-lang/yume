#include "ast.hpp"

#include "qualifier.hpp"
#include "util.hpp"
#include <llvm/ADT/STLExtras.h>
#include <sstream>
#include <string>

namespace yume::ast {

auto AST::describe() const -> string { return string{"unknown "} + kind_name(); }
auto QualType::describe() const -> string {
  switch (m_qualifier) {
  case Qualifier::Ptr: return "ptr";
  case Qualifier::Slice: return "slice";
  case Qualifier::Mut: return "mut";
  default: return "";
  }
}
auto TemplatedType::describe() const -> string {
  stringstream ss{};
  ss << "{";
  for (const auto& i : llvm::enumerate(m_type_args)) {
    if (i.index() > 0)
      ss << ",";
    ss << i.value()->describe();
  }
  ss << "}";
  return ss.str();
}
auto SimpleType::describe() const -> string { return m_name; }
auto SelfType::describe() const -> string { return "self"; }
auto TypeName::describe() const -> string { return name; }
auto NumberExpr::describe() const -> string { return std::to_string(m_val); }
auto CharExpr::describe() const -> string { return std::to_string(m_val); }
auto BoolExpr::describe() const -> string { return m_val ? "true" : "false"; }
auto StringExpr::describe() const -> string { return m_val; }
auto VarExpr::describe() const -> string { return m_name; }
auto CallExpr::describe() const -> string { return m_name; }
auto CtorExpr::describe() const -> string { return m_type->describe(); }
auto DtorExpr::describe() const -> string { return m_base->describe(); }
auto SliceExpr::describe() const -> string { return m_type->describe(); }
auto FnDecl::describe() const -> string { return m_name; }
auto CtorDecl::describe() const -> string { return ":new"; }
auto StructDecl::describe() const -> string { return m_name; }
auto VarDecl::describe() const -> string { return m_name; }
} // namespace yume::ast
