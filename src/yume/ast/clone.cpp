#include "ast.hpp"

#include "util.hpp"
#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace yume::ast {

namespace {
template <typename T> auto dup(const vector<unique_ptr<T>>& items) {
  auto dup = vector<unique_ptr<T>>();
  dup.reserve(items.size());
  for (auto& i : items)
    dup.push_back(unique_ptr<T>(i->clone()));

  return dup;
}

template <typename T> auto dup(const vector<T>& items) {
  auto dup = vector<T>();
  dup.reserve(items.size());
  for (auto& i : items) {
    auto* cloned = i.clone();
    dup.push_back(move(*cloned));
    delete cloned; // Need to free the cloned object even though it was moved from
  }

  return dup;
}

template <typename T> auto dup(const unique_ptr<T>& ptr) { return unique_ptr<T>(ptr->clone()); }

template <typename T> auto dup(const T& ast) -> T {
  auto cloned = unique_ptr<T>(ast.clone());
  return move(*cloned);
}

template <typename T> auto dup(const optional<T>& opt) {
  return opt.has_value() ? optional<T>{dup(opt.value())} : optional<T>{};
}
} // namespace

auto IfStmt::clone() const -> IfStmt* { return new IfStmt(tok(), dup(m_clauses), dup(m_else_clause)); }
auto IfClause::clone() const -> IfClause* { return new IfClause(tok(), dup(m_cond), dup(m_body)); }
auto NumberExpr::clone() const -> NumberExpr* { return new NumberExpr(tok(), m_val); }
auto StringExpr::clone() const -> StringExpr* { return new StringExpr(tok(), m_val); }
auto CharExpr::clone() const -> CharExpr* { return new CharExpr(tok(), m_val); }
auto BoolExpr::clone() const -> BoolExpr* { return new BoolExpr(tok(), m_val); }
auto ReturnStmt::clone() const -> ReturnStmt* { return new ReturnStmt(tok(), dup(m_expr)); }
auto WhileStmt::clone() const -> WhileStmt* { return new WhileStmt(tok(), dup(m_cond), dup(m_body)); }
auto VarDecl::clone() const -> VarDecl* { return new VarDecl(tok(), m_name, dup(m_type), dup(m_init)); }
auto FnDecl::clone() const -> FnDecl* {
  if (const auto* s = get_if<string>(&m_body); s)
    return new FnDecl(tok(), m_name, dup(m_args), m_type_args, dup(m_ret), m_varargs, *s);
  return new FnDecl(tok(), m_name, dup(m_args), m_type_args, dup(m_ret), dup(get<Compound>(m_body)));
}
auto StructDecl::clone() const -> StructDecl* {
  return new StructDecl(tok(), m_name, dup(m_fields), m_type_args, dup(m_body));
}
auto SimpleType::clone() const -> SimpleType* { return new SimpleType(tok(), m_name); }
auto QualType::clone() const -> QualType* { return new QualType(tok(), dup(m_base), m_qualifier); }
auto TemplatedType::clone() const -> TemplatedType* { return new TemplatedType(tok(), dup(m_base), dup(m_type_args)); }
auto SelfType::clone() const -> SelfType* { return new SelfType(tok()); }
auto TypeName::clone() const -> TypeName* { return new TypeName(tok(), dup(m_type), m_name); }
auto Compound::clone() const -> Compound* { return new Compound(tok(), dup(m_body)); }
auto VarExpr::clone() const -> VarExpr* { return new VarExpr(tok(), m_name); }
auto CallExpr::clone() const -> CallExpr* { return new CallExpr(tok(), m_name, dup(m_args)); }
auto CtorExpr::clone() const -> CtorExpr* { return new CtorExpr(tok(), dup(m_type), dup(m_args)); }
auto DtorExpr::clone() const -> DtorExpr* { return new DtorExpr(tok(), dup(m_base)); }
auto SliceExpr::clone() const -> SliceExpr* { return new SliceExpr(tok(), dup(m_type), dup(m_args)); }
auto AssignExpr::clone() const -> AssignExpr* { return new AssignExpr(tok(), dup(m_target), dup(m_value)); }
auto FieldAccessExpr::clone() const -> FieldAccessExpr* { return new FieldAccessExpr(tok(), dup(m_base), m_field); }
auto ImplicitCastExpr::clone() const -> ImplicitCastExpr* {
  return new ImplicitCastExpr(tok(), dup(m_base), m_conversion);
}
auto Program::clone() const -> Program* { return new Program(tok(), dup(m_body)); }

} // namespace yume::ast
