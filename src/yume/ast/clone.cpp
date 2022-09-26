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
template <typename T> auto dup(const vector<AnyBase<T>>& items) {
  auto dup = vector<AnyBase<T>>();
  dup.reserve(items.size());
  for (auto& i : items)
    dup.emplace_back(unique_ptr<T>(i->clone()));

  return dup;
}

template <typename T> auto dup(const OptionalAnyBase<T>& ptr) -> OptionalAnyBase<T> {
  if (ptr)
    return unique_ptr<T>(ptr->clone());
  return {};
}
template <typename T> auto dup(const AnyBase<T>& ptr) -> AnyBase<T> { return unique_ptr<T>(ptr->clone()); }

template <std::derived_from<ast::AST> T> auto dup(const T& ast) -> T {
  auto cloned = unique_ptr<T>(ast.clone());
  return move(*cloned);
}

template <std::copy_constructible T> auto dup(const T& obj) -> T { return T(obj); }

template <typename T, typename U> auto dup(const std::variant<T, U>& var) -> std::variant<T, U> {
  if (std::holds_alternative<T>(var))
    return dup(std::get<T>(var));
  return dup(std::get<U>(var));
}

template <typename T> auto dup(const optional<T>& opt) {
  return opt.has_value() ? optional<T>{dup(opt.value())} : optional<T>{};
}

template <typename T> auto dup(const vector<T>& items) {
  auto dup_vec = vector<T>();
  dup_vec.reserve(items.size());
  for (auto& i : items) {
    dup_vec.push_back(move(dup(i)));
  }

  return dup_vec;
}
} // namespace

auto IfStmt::clone() const -> IfStmt* { return new IfStmt(tok(), dup(clauses), dup(else_clause)); }
auto IfClause::clone() const -> IfClause* { return new IfClause(tok(), dup(cond), dup(body)); }
auto NumberExpr::clone() const -> NumberExpr* { return new NumberExpr(tok(), m_val); }
auto StringExpr::clone() const -> StringExpr* { return new StringExpr(tok(), m_val); }
auto CharExpr::clone() const -> CharExpr* { return new CharExpr(tok(), m_val); }
auto BoolExpr::clone() const -> BoolExpr* { return new BoolExpr(tok(), m_val); }
auto ReturnStmt::clone() const -> ReturnStmt* { return new ReturnStmt(tok(), dup(expr)); }
auto WhileStmt::clone() const -> WhileStmt* { return new WhileStmt(tok(), dup(cond), dup(body)); }
auto VarDecl::clone() const -> VarDecl* { return new VarDecl(tok(), m_name, dup(m_type), dup(m_init)); }
auto ConstDecl::clone() const -> ConstDecl* { return new ConstDecl(tok(), m_name, dup(m_type), dup(m_init)); }
auto FnDecl::clone() const -> FnDecl* {
  return std::visit(
      [&](auto& s) {
        return new FnDecl(tok(), m_name, dup(m_args), m_type_args, dup(m_ret), dup(s), dup(m_annotations));
      },
      m_body);
}
auto CtorDecl::clone() const -> CtorDecl* { return new CtorDecl(tok(), dup(m_args), dup(m_body)); }
auto StructDecl::clone() const -> StructDecl* {
  return new StructDecl(tok(), m_name, dup(m_fields), m_type_args, dup(m_body));
}
auto SimpleType::clone() const -> SimpleType* { return new SimpleType(tok(), m_name); }
auto QualType::clone() const -> QualType* { return new QualType(tok(), dup(m_base), m_qualifier); }
auto TemplatedType::clone() const -> TemplatedType* { return new TemplatedType(tok(), dup(m_base), dup(m_type_args)); }
auto SelfType::clone() const -> SelfType* { return new SelfType(tok()); }
auto ProxyType::clone() const -> ProxyType* { return new ProxyType(tok(), m_field); }
auto FunctionType::clone() const -> FunctionType* { return new FunctionType(tok(), dup(m_ret), dup(m_args), m_fn_ptr); }
auto TypeName::clone() const -> TypeName* { return new TypeName(tok(), dup(type), name); }
auto Compound::clone() const -> Compound* { return new Compound(tok(), dup(m_body)); }
auto VarExpr::clone() const -> VarExpr* { return new VarExpr(tok(), m_name); }
auto ConstExpr::clone() const -> ConstExpr* { return new ConstExpr(tok(), m_name, m_parent); }
auto CallExpr::clone() const -> CallExpr* { return new CallExpr(tok(), m_name, dup(m_args)); }
auto CtorExpr::clone() const -> CtorExpr* { return new CtorExpr(tok(), dup(m_type), dup(m_args)); }
auto DtorExpr::clone() const -> DtorExpr* { return new DtorExpr(tok(), dup(m_base)); }
auto SliceExpr::clone() const -> SliceExpr* { return new SliceExpr(tok(), dup(m_type), dup(m_args)); }
auto LambdaExpr::clone() const -> LambdaExpr* {
  return new LambdaExpr(tok(), dup(m_args), dup(m_ret), dup(m_body), dup(m_annotations));
}
auto DirectCallExpr::clone() const -> DirectCallExpr* { return new DirectCallExpr(tok(), dup(m_base), dup(m_args)); }
auto AssignExpr::clone() const -> AssignExpr* { return new AssignExpr(tok(), dup(m_target), dup(m_value)); }
auto FieldAccessExpr::clone() const -> FieldAccessExpr* { return new FieldAccessExpr(tok(), dup(m_base), m_field); }
auto ImplicitCastExpr::clone() const -> ImplicitCastExpr* {
  return new ImplicitCastExpr(tok(), dup(m_base), m_conversion);
}
auto Program::clone() const -> Program* { return new Program(tok(), dup(body)); }

} // namespace yume::ast
