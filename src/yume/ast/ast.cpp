#include "ast.hpp"

#include "token.hpp"
#include <memory>
#include <stdexcept>

namespace yume::ast {

void AST::unify_val_ty() {
  for (const auto* other : m_attach->depends) {
    if (m_val_ty == other->m_val_ty || !other->m_val_ty)
      return;

    if (!m_val_ty) {
      m_val_ty = other->m_val_ty;
    } else {
      const auto merged = m_val_ty->coalesce(*other->m_val_ty);
      if (!merged) {
        throw std::logic_error("Conflicting types between AST nodes that are attached: `"s + m_val_ty->name() +
                               "` vs `" + other->m_val_ty->name() + "`!");
      }
      m_val_ty = merged;
    }
  }
}

auto AST::location() const -> Loc {
  if (m_tok.empty())
    return Loc{};
  if (m_tok.size() == 1)
    return m_tok[0].loc;

  return m_tok[0].loc + m_tok[m_tok.size() - 1].loc;
}

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

template <typename... Ts> [[deprecated]] auto dup(const std::variant<Ts...>& var) -> std::variant<Ts...> {
  return std::visit([](auto&& x) { return std::variant<Ts...>{dup(std::forward<decltype(x)>(x))}; }, var);
}

template <visitable T> auto dup(const T& var) -> T {
  return var.visit([](auto&& x) { return T{dup(std::forward<decltype(x)>(x))}; });
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
auto NumberExpr::clone() const -> NumberExpr* { return new NumberExpr(tok(), val); }
auto StringExpr::clone() const -> StringExpr* { return new StringExpr(tok(), val); }
auto CharExpr::clone() const -> CharExpr* { return new CharExpr(tok(), val); }
auto BoolExpr::clone() const -> BoolExpr* { return new BoolExpr(tok(), val); }
auto ReturnStmt::clone() const -> ReturnStmt* { return new ReturnStmt(tok(), dup(expr)); }
auto WhileStmt::clone() const -> WhileStmt* { return new WhileStmt(tok(), dup(cond), dup(body)); }
auto VarDecl::clone() const -> VarDecl* { return new VarDecl(tok(), name, dup(type), dup(init)); }
auto ConstDecl::clone() const -> ConstDecl* { return new ConstDecl(tok(), name, dup(type), dup(init)); }
auto FnDecl::clone() const -> FnDecl* {
  return new FnDecl(tok(), name, dup(args), type_args, dup(ret), dup(body), dup(annotations));
}
auto CtorDecl::clone() const -> CtorDecl* { return new CtorDecl(tok(), dup(args), dup(body)); }
auto StructDecl::clone() const -> StructDecl* {
  return new StructDecl(tok(), name, dup(fields), type_args, dup(body), dup(implements), is_interface);
}
auto SimpleType::clone() const -> SimpleType* { return new SimpleType(tok(), name); }
auto QualType::clone() const -> QualType* { return new QualType(tok(), dup(base), qualifier); }
auto TemplatedType::clone() const -> TemplatedType* { return new TemplatedType(tok(), dup(base), dup(type_args)); }
auto SelfType::clone() const -> SelfType* { return new SelfType(tok()); }
auto ProxyType::clone() const -> ProxyType* { return new ProxyType(tok(), field); }
auto FunctionType::clone() const -> FunctionType* { return new FunctionType(tok(), dup(ret), dup(args), fn_ptr); }
auto TypeName::clone() const -> TypeName* { return new TypeName(tok(), dup(type), name); }
auto Compound::clone() const -> Compound* { return new Compound(tok(), dup(body)); }
auto VarExpr::clone() const -> VarExpr* { return new VarExpr(tok(), name); }
auto ConstExpr::clone() const -> ConstExpr* { return new ConstExpr(tok(), name, parent); }
auto CallExpr::clone() const -> CallExpr* { return new CallExpr(tok(), name, dup(receiver), dup(args)); }
auto BinaryLogicExpr::clone() const -> BinaryLogicExpr* {
  return new BinaryLogicExpr(tok(), operation, dup(lhs), dup(rhs));
}
auto CtorExpr::clone() const -> CtorExpr* { return new CtorExpr(tok(), dup(type), dup(args)); }
auto DtorExpr::clone() const -> DtorExpr* { return new DtorExpr(tok(), dup(base)); }
auto SliceExpr::clone() const -> SliceExpr* { return new SliceExpr(tok(), dup(type), dup(args)); }
auto LambdaExpr::clone() const -> LambdaExpr* {
  return new LambdaExpr(tok(), dup(args), dup(ret), dup(body), dup(annotations));
}
auto AssignExpr::clone() const -> AssignExpr* { return new AssignExpr(tok(), dup(target), dup(value)); }
auto FieldAccessExpr::clone() const -> FieldAccessExpr* { return new FieldAccessExpr(tok(), dup(base), field); }
auto ImplicitCastExpr::clone() const -> ImplicitCastExpr* { return new ImplicitCastExpr(tok(), dup(base), conversion); }
auto TypeExpr::clone() const -> TypeExpr* { return new TypeExpr(tok(), dup(type)); }
auto Program::clone() const -> Program* { return new Program(tok(), dup(body)); }
} // namespace yume::ast
